#include "media/MediaPlayer.h"

#include "app/App.h"

#include <mfapi.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <new>

// Media Foundation + Direct3D import libraries. Declared here (rather than in
// the vcxproj) to keep the backend's dependencies self-contained, matching the
// #pragma comment(lib, ...) style already used in main.cpp.
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "d3d11.lib")

namespace
{
	using Microsoft::WRL::ComPtr;

	// -------------------------------------------------------------------------
	// Encapsulated player state. The Media Engine keeps everything private to
	// this translation unit and reaches into App.h only for the shared
	// user-preference flags (g_muted / g_autoMuted / g_fillScreen / g_autoPaused).
	// -------------------------------------------------------------------------
	bool                         s_mfStarted      = false;
	ComPtr<ID3D11Device>         s_d3dDevice;
	ComPtr<IMFDXGIDeviceManager> s_dxgiManager;
	ComPtr<IMFMediaEngine>       s_engine;
	ComPtr<IMFMediaEngineEx>     s_engineEx;     // optional; enables crop-to-fill
	ComPtr<IMFMediaEngineNotify> s_notify;
	HWND                         s_hwnd           = nullptr;

	// Last value handed to SetMuted; -1 means "unknown / force re-apply". Avoids
	// redundant SetMuted calls from the occlusion watcher on the UI thread.
	int                          s_lastMuteApplied = -1;

	void OnMediaEngineEvent(DWORD event, DWORD_PTR param1, DWORD param2);

	// IMFMediaEngineNotify is mandatory: the class factory refuses to create an
	// engine without a callback. It simply forwards engine events to the
	// file-scope handler below. Events arrive on a Media Foundation worker
	// thread.
	class MediaEngineNotify final : public IMFMediaEngineNotify
	{
	public:
		MediaEngineNotify() = default;

		// IUnknown -----------------------------------------------------------
		STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
		{
			if (!ppv)
			{
				return E_POINTER;
			}
			if (riid == __uuidof(IUnknown) || riid == __uuidof(IMFMediaEngineNotify))
			{
				*ppv = static_cast<IMFMediaEngineNotify*>(this);
				AddRef();
				return S_OK;
			}
			*ppv = nullptr;
			return E_NOINTERFACE;
		}

		STDMETHODIMP_(ULONG) AddRef() override
		{
			return InterlockedIncrement(&m_ref);
		}

		STDMETHODIMP_(ULONG) Release() override
		{
			const ULONG count = InterlockedDecrement(&m_ref);
			if (count == 0)
			{
				delete this;
			}
			return count;
		}

		// IMFMediaEngineNotify ----------------------------------------------
		STDMETHODIMP EventNotify(DWORD event, DWORD_PTR param1, DWORD param2) override
		{
			OnMediaEngineEvent(event, param1, param2);
			return S_OK;
		}

	private:
		LONG m_ref = 1;
	};

	// Points the engine at a local file. SetSource mirrors the HTML5 <video>
	// src attribute: assigning it implicitly (re)loads the media, which fires
	// LOADEDMETADATA then CANPLAY on the notify callback.
	HRESULT SetEngineSource(PCWSTR path)
	{
		BSTR url = SysAllocString(path);
		if (!url)
		{
			return E_OUTOFMEMORY;
		}
		const HRESULT hr = s_engine->SetSource(url);
		SysFreeString(url);
		return hr;
	}

	// Creates the D3D11 device the Media Engine uses to hardware-decode and
	// present. BGRA support is required for the video output format; VIDEO
	// support enables the hardware decoder. Falls back to WARP (software) if no
	// GPU device is available so playback never fails outright.
	HRESULT CreateD3D11Device()
	{
		static const D3D_FEATURE_LEVEL kLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		const UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT;

		HRESULT hr = D3D11CreateDevice(
			nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
			kLevels, ARRAYSIZE(kLevels), D3D11_SDK_VERSION,
			s_d3dDevice.ReleaseAndGetAddressOf(), nullptr, nullptr);
		if (FAILED(hr))
		{
			hr = D3D11CreateDevice(
				nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
				kLevels, ARRAYSIZE(kLevels), D3D11_SDK_VERSION,
				s_d3dDevice.ReleaseAndGetAddressOf(), nullptr, nullptr);
		}
		if (FAILED(hr))
		{
			return hr;
		}

		// The device is shared between the engine's decode thread and its
		// presenter, so it must allow multithreaded access.
		ComPtr<ID3D10Multithread> multithread;
		if (SUCCEEDED(s_d3dDevice.As(&multithread)))
		{
			multithread->SetMultithreadProtected(TRUE);
		}
		return S_OK;
	}

	// Releases everything in dependency order. Calling Shutdown() first stops
	// the engine dispatching further events before we drop our references.
	void ReleaseAll()
	{
		if (s_engine)
		{
			s_engine->Shutdown();
		}
		s_engineEx.Reset();
		s_engine.Reset();
		s_notify.Reset();
		s_dxgiManager.Reset();
		s_d3dDevice.Reset();
		s_hwnd = nullptr;
		s_lastMuteApplied = -1;
		if (s_mfStarted)
		{
			MFShutdown();
			s_mfStarted = false;
		}
	}

	void OnMediaEngineEvent(DWORD event, DWORD_PTR /*param1*/, DWORD /*param2*/)
	{
		switch (event)
		{
		case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
			// Native video size is known now: (re)apply crop / letterbox for
			// the new clip's dimensions.
			ApplyAspectMode(s_hwnd);
			break;

		case MF_MEDIA_ENGINE_EVENT_CANPLAY:
			// Start playback unless a pause reason (fullscreen app, display
			// off, session locked) is currently active — otherwise we would
			// override an auto-pause that raced ahead of the load.
			if (s_engine && !g_autoPaused)
			{
				s_engine->Play();
			}
			ApplyAspectMode(s_hwnd);
			break;

		default:
			break;
		}
	}
}

HRESULT InitPlayer(HWND hwnd, PCWSTR initialPath)
{
	if (!hwnd || !initialPath || !*initialPath)
	{
		return E_INVALIDARG;
	}
	if (s_engine)
	{
		return S_OK; // already initialized
	}

	HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
	if (FAILED(hr))
	{
		return hr;
	}
	s_mfStarted = true;

	hr = CreateD3D11Device();
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}

	// Wrap the device in a DXGI device manager so the engine can share it.
	UINT resetToken = 0;
	hr = MFCreateDXGIDeviceManager(&resetToken, &s_dxgiManager);
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}
	hr = s_dxgiManager->ResetDevice(s_d3dDevice.Get(), resetToken);
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}

	ComPtr<MediaEngineNotify> notify;
	notify.Attach(new (std::nothrow) MediaEngineNotify());
	if (!notify)
	{
		ReleaseAll();
		return E_OUTOFMEMORY;
	}

	// Engine creation attributes:
	//   DXGI_MANAGER         - share our D3D11 device for HW decode + present.
	//   CALLBACK             - the (mandatory) event sink.
	//   PLAYBACK_HWND        - render directly into the wallpaper window.
	//   VIDEO_OUTPUT_FORMAT  - match the BGRA device we created.
	ComPtr<IMFAttributes> attributes;
	hr = MFCreateAttributes(&attributes, 4);
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}
	attributes->SetUnknown(MF_MEDIA_ENGINE_DXGI_MANAGER, s_dxgiManager.Get());
	attributes->SetUnknown(MF_MEDIA_ENGINE_CALLBACK, notify.Get());
	attributes->SetUINT64(MF_MEDIA_ENGINE_PLAYBACK_HWND, reinterpret_cast<UINT64>(hwnd));
	attributes->SetUINT32(MF_MEDIA_ENGINE_VIDEO_OUTPUT_FORMAT, DXGI_FORMAT_B8G8R8A8_UNORM);

	ComPtr<IMFMediaEngineClassFactory> factory;
	hr = CoCreateInstance(
		CLSID_MFMediaEngineClassFactory, nullptr,
		CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory));
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}

	hr = factory->CreateInstance(0, attributes.Get(), s_engine.ReleaseAndGetAddressOf());
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}

	s_engine.As(&s_engineEx); // Win8+; null-tolerant (only crop-to-fill needs it)
	s_notify = notify;
	s_hwnd   = hwnd;

	s_engine->SetLoop(TRUE);
	s_engine->SetVolume(1.0);

	hr = SetEngineSource(initialPath);
	if (FAILED(hr))
	{
		ReleaseAll();
		return hr;
	}

	s_lastMuteApplied = -1;
	ApplyEffectiveMute();

	// Playback begins from the CANPLAY handler; crop is applied from
	// LOADEDMETADATA once the native video size is known.
	return S_OK;
}

void ShutdownPlayer()
{
	ReleaseAll();
}

void PauseWallpaper()
{
	if (s_engine)
	{
		s_engine->Pause();
	}
}

void ResumeWallpaper()
{
	if (s_engine)
	{
		s_engine->Play();
	}
}

void ApplyEffectiveMute()
{
	if (!s_engine)
	{
		return;
	}
	const BOOL desired = (g_muted || g_autoMuted) ? TRUE : FALSE;
	if (s_lastMuteApplied == static_cast<int>(desired))
	{
		return;
	}
	s_engine->SetMuted(desired);
	s_lastMuteApplied = desired;
}

HRESULT PlayWallpaperPath(PCWSTR path)
{
	if (!s_engine || !path || !*path)
	{
		return E_INVALIDARG;
	}

	// Re-pointing SetSource reloads the pipeline; the engine releases the
	// previous decoder / presenter for us (no manual stop required).
	HRESULT hr = SetEngineSource(path);
	if (FAILED(hr))
	{
		return hr;
	}

	s_engine->SetLoop(TRUE);

	// Force mute re-apply to the fresh pipeline; CANPLAY restarts playback and
	// LOADEDMETADATA re-applies the crop for the new clip's dimensions.
	s_lastMuteApplied = -1;
	ApplyEffectiveMute();
	return S_OK;
}

void RestartWallpaper()
{
	if (s_engine)
	{
		s_engine->SetCurrentTime(0.0);
	}
}

void ApplyAspectMode(HWND hwnd)
{
	if (!s_engine)
	{
		return;
	}

	HWND target = (hwnd && IsWindow(hwnd)) ? hwnd : s_hwnd;
	if (!target || !IsWindow(target))
	{
		return;
	}

	DWORD vw = 0;
	DWORD vh = 0;
	if (FAILED(s_engine->GetNativeVideoSize(&vw, &vh)) || vw == 0 || vh == 0)
	{
		return; // metadata not loaded yet; LOADEDMETADATA will call us again
	}

	// Without IMFMediaEngineEx we cannot set source/destination rects; the
	// engine letterboxes by default, which is the non-fill behaviour anyway.
	if (!s_engineEx)
	{
		return;
	}

	RECT rc{};
	if (!GetClientRect(target, &rc))
	{
		return;
	}
	const LONG ww = rc.right - rc.left;
	const LONG wh = rc.bottom - rc.top;
	if (ww <= 0 || wh <= 0)
	{
		return;
	}

	const double winAspect = static_cast<double>(ww) / wh;
	const double vidAspect = static_cast<double>(vw) / vh;

	MFVideoNormalizedRect src{ 0.0f, 0.0f, 1.0f, 1.0f };
	RECT dst{ 0, 0, ww, wh };

	if (g_fillScreen)
	{
		// Crop the source to the window aspect, then stretch the cropped region
		// over the whole window. Because the cropped region already has the
		// window's aspect ratio, the stretch introduces no distortion.
		if (winAspect >= vidAspect)
		{
			// Window is wider than the video: crop top & bottom.
			const double keep = vidAspect / winAspect;
			const float inset = static_cast<float>((1.0 - keep) / 2.0);
			src.top    = inset;
			src.bottom = 1.0f - inset;
		}
		else
		{
			// Window is taller / narrower than the video: crop left & right.
			const double keep = winAspect / vidAspect;
			const float inset = static_cast<float>((1.0 - keep) / 2.0);
			src.left  = inset;
			src.right = 1.0f - inset;
		}
	}
	else
	{
		// Letterbox: keep the full source but shrink the destination to the
		// video aspect and center it. The window's black background paints the
		// bars.
		if (vidAspect >= winAspect)
		{
			const LONG h = static_cast<LONG>(ww / vidAspect + 0.5);
			const LONG y = (wh - h) / 2;
			dst = { 0, y, ww, y + h };
		}
		else
		{
			const LONG w = static_cast<LONG>(wh * vidAspect + 0.5);
			const LONG x = (ww - w) / 2;
			dst = { x, 0, x + w, wh };
		}
	}

	s_engineEx->UpdateVideoStream(&src, &dst, nullptr);
}
