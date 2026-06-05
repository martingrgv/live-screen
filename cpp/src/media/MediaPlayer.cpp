#include "media/MediaPlayer.h"

#include "app/App.h"

#include <mfapi.h>
#include <mfmediaengine.h>
#include <d3d11.h>
#include <wrl/client.h>

#include <memory>
#include <new>
#include <string>
#include <vector>

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
	// One Media Engine bound to one renderer window. Multi-monitor layouts use
	// several of these; they share the D3D11 device / DXGI manager below.
	// -------------------------------------------------------------------------
	struct EngineInstance
	{
		ComPtr<IMFMediaEngine>       engine;
		ComPtr<IMFMediaEngineEx>     engineEx;   // optional; enables crop-to-fill
		ComPtr<IMFMediaEngineNotify> notify;
		HWND                         hwnd            = nullptr;
		// Last value handed to SetMuted; -1 means "unknown / force re-apply".
		int                          lastMuteApplied = -1;
	};

	// -------------------------------------------------------------------------
	// Shared, process-wide player state. The engines reach into App.h only for
	// the shared user-preference flags (g_muted / g_autoMuted / g_fillScreen /
	// g_autoPaused).
	// -------------------------------------------------------------------------
	bool                         s_mfStarted = false;
	ComPtr<ID3D11Device>         s_d3dDevice;
	ComPtr<IMFDXGIDeviceManager> s_dxgiManager;
	std::wstring                 s_currentPath;

	// Stable storage: unique_ptr keeps each instance's address fixed even as the
	// vector grows, so the notify callback can hold a raw EngineInstance*.
	std::vector<std::unique_ptr<EngineInstance>> s_engines;

	void OnEngineEvent(EngineInstance* inst, DWORD event);

	// IMFMediaEngineNotify is mandatory: the class factory refuses to create an
	// engine without a callback. It forwards engine events, tagged with the
	// owning instance, to the file-scope handler below. Events arrive on a Media
	// Foundation worker thread.
	class MediaEngineNotify final : public IMFMediaEngineNotify
	{
	public:
		explicit MediaEngineNotify(EngineInstance* owner) : m_owner(owner) {}

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
		STDMETHODIMP EventNotify(DWORD event, DWORD_PTR /*param1*/, DWORD /*param2*/) override
		{
			OnEngineEvent(m_owner, event);
			return S_OK;
		}

	private:
		EngineInstance* m_owner;
		LONG            m_ref = 1;
	};

	// Points an engine at a local file. SetSource mirrors the HTML5 <video> src
	// attribute: assigning it implicitly (re)loads the media, which fires
	// LOADEDMETADATA then CANPLAY on the notify callback.
	HRESULT SetEngineSource(IMFMediaEngine* engine, PCWSTR path)
	{
		BSTR url = SysAllocString(path);
		if (!url)
		{
			return E_OUTOFMEMORY;
		}
		const HRESULT hr = engine->SetSource(url);
		SysFreeString(url);
		return hr;
	}

	// Creates the D3D11 device the Media Engines use to hardware-decode and
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

		// The device is shared between the engines' decode threads and their
		// presenters, so it must allow multithreaded access.
		ComPtr<ID3D10Multithread> multithread;
		if (SUCCEEDED(s_d3dDevice.As(&multithread)))
		{
			multithread->SetMultithreadProtected(TRUE);
		}
		return S_OK;
	}

	// Applies the current g_fillScreen preference to one instance, using that
	// instance's own window aspect.
	void ApplyAspectToInstance(EngineInstance& inst)
	{
		if (!inst.engine || !inst.engineEx)
		{
			// Without IMFMediaEngineEx we cannot set source/destination rects;
			// the engine letterboxes by default, which is the non-fill behaviour.
			return;
		}
		if (!inst.hwnd || !IsWindow(inst.hwnd))
		{
			return;
		}

		DWORD vw = 0;
		DWORD vh = 0;
		if (FAILED(inst.engine->GetNativeVideoSize(&vw, &vh)) || vw == 0 || vh == 0)
		{
			return; // metadata not loaded yet; LOADEDMETADATA will call us again
		}

		RECT rc{};
		if (!GetClientRect(inst.hwnd, &rc))
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
			// Crop the source to the window aspect, then stretch the cropped
			// region over the whole window. Because the cropped region already
			// has the window's aspect ratio, the stretch introduces no distortion.
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
			// video aspect and center it. The window's black background paints
			// the bars.
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

		inst.engineEx->UpdateVideoStream(&src, &dst, nullptr);
	}

	// Applies mute to one instance. Only the designated audio instance follows
	// the user's mute preference; every other instance is force-muted so audio
	// isn't duplicated across monitors.
	void ApplyMuteToInstance(EngineInstance& inst, bool isAudio)
	{
		if (!inst.engine)
		{
			return;
		}
		const BOOL desired = (!isAudio || g_muted || g_autoMuted) ? TRUE : FALSE;
		if (inst.lastMuteApplied == static_cast<int>(desired))
		{
			return;
		}
		inst.engine->SetMuted(desired);
		inst.lastMuteApplied = desired;
	}

	// Chooses which engine is allowed to play audio: the one on the primary
	// monitor, or the first engine when the primary monitor has no wallpaper
	// (e.g. Specific mode with the primary disabled).
	size_t PickAudioInstance()
	{
		for (size_t i = 0; i < s_engines.size(); ++i)
		{
			if (!s_engines[i] || !s_engines[i]->hwnd)
			{
				continue;
			}
			HMONITOR mon = MonitorFromWindow(s_engines[i]->hwnd, MONITOR_DEFAULTTONEAREST);
			MONITORINFO mi{ sizeof(mi) };
			if (mon && GetMonitorInfo(mon, &mi) && (mi.dwFlags & MONITORINFOF_PRIMARY))
			{
				return i;
			}
		}
		return 0;
	}

	void OnEngineEvent(EngineInstance* inst, DWORD event)
	{
		if (!inst)
		{
			return;
		}
		switch (event)
		{
		case MF_MEDIA_ENGINE_EVENT_LOADEDMETADATA:
			// Native video size is known now: (re)apply crop / letterbox for the
			// new clip's dimensions.
			ApplyAspectToInstance(*inst);
			break;

		case MF_MEDIA_ENGINE_EVENT_CANPLAY:
			// Start playback unless a pause reason (fullscreen app, display off,
			// session locked) is currently active — otherwise we would override
			// an auto-pause that raced ahead of the load.
			if (inst->engine && !g_autoPaused)
			{
				inst->engine->Play();
			}
			ApplyAspectToInstance(*inst);
			break;

		default:
			break;
		}
	}

	// Creates one engine for `hwnd` rendering the current path. On success the
	// new instance is appended to s_engines.
	HRESULT CreateEngineForWindow(HWND hwnd)
	{
		auto inst = std::make_unique<EngineInstance>();
		inst->hwnd = hwnd;

		ComPtr<MediaEngineNotify> notify;
		notify.Attach(new (std::nothrow) MediaEngineNotify(inst.get()));
		if (!notify)
		{
			return E_OUTOFMEMORY;
		}

		// Engine creation attributes:
		//   DXGI_MANAGER         - share our D3D11 device for HW decode + present.
		//   CALLBACK             - the (mandatory) event sink.
		//   PLAYBACK_HWND        - render directly into this renderer window.
		//   VIDEO_OUTPUT_FORMAT  - match the BGRA device we created.
		ComPtr<IMFAttributes> attributes;
		HRESULT hr = MFCreateAttributes(&attributes, 4);
		if (FAILED(hr))
		{
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
			return hr;
		}

		hr = factory->CreateInstance(0, attributes.Get(), inst->engine.ReleaseAndGetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		inst->engine.As(&inst->engineEx); // Win8+; null-tolerant (only crop-to-fill needs it)
		inst->notify = notify;

		inst->engine->SetLoop(TRUE);
		inst->engine->SetVolume(1.0);

		hr = SetEngineSource(inst->engine.Get(), s_currentPath.c_str());
		if (FAILED(hr))
		{
			inst->engine->Shutdown();
			return hr;
		}

		// Playback begins from the CANPLAY handler; crop is applied from
		// LOADEDMETADATA once the native video size is known. Mute is applied by
		// the caller (ApplyEffectiveMute) across all engines.
		s_engines.push_back(std::move(inst));
		return S_OK;
	}
}

HRESULT EnsurePlayerStarted(PCWSTR initialPath)
{
	if (!initialPath || !*initialPath)
	{
		return E_INVALIDARG;
	}
	s_currentPath = initialPath;

	if (s_mfStarted && s_d3dDevice && s_dxgiManager)
	{
		return S_OK; // already started
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
		ShutdownPlayer();
		return hr;
	}

	// Wrap the device in a DXGI device manager so the engines can share it.
	UINT resetToken = 0;
	hr = MFCreateDXGIDeviceManager(&resetToken, &s_dxgiManager);
	if (FAILED(hr))
	{
		ShutdownPlayer();
		return hr;
	}
	hr = s_dxgiManager->ResetDevice(s_d3dDevice.Get(), resetToken);
	if (FAILED(hr))
	{
		ShutdownPlayer();
		return hr;
	}

	return S_OK;
}

HRESULT SetRenderTargets(const std::vector<HWND>& windows)
{
	// Release any existing engines before creating the new ones so we never have
	// two engines presenting to overlapping windows.
	ReleaseRenderTargets();

	if (!s_d3dDevice || !s_dxgiManager)
	{
		return E_FAIL; // EnsurePlayerStarted not called / failed
	}

	HRESULT firstErr = S_OK;
	for (HWND hwnd : windows)
	{
		if (!hwnd)
		{
			continue;
		}
		HRESULT hr = CreateEngineForWindow(hwnd);
		if (FAILED(hr) && SUCCEEDED(firstErr))
		{
			firstErr = hr;
		}
	}

	// Apply the user's mute preference to the freshly created engines.
	ApplyEffectiveMute();
	return firstErr;
}

void ReleaseRenderTargets()
{
	// Calling Shutdown() first stops each engine dispatching further events
	// before we drop our references (and its renderer window is destroyed).
	for (auto& inst : s_engines)
	{
		if (inst && inst->engine)
		{
			inst->engine->Shutdown();
		}
	}
	s_engines.clear();
}

void ShutdownPlayer()
{
	ReleaseRenderTargets();
	s_dxgiManager.Reset();
	s_d3dDevice.Reset();
	if (s_mfStarted)
	{
		MFShutdown();
		s_mfStarted = false;
	}
	s_currentPath.clear();
}

void PauseWallpaper()
{
	for (auto& inst : s_engines)
	{
		if (inst && inst->engine)
		{
			inst->engine->Pause();
		}
	}
}

void ResumeWallpaper()
{
	for (auto& inst : s_engines)
	{
		if (inst && inst->engine)
		{
			inst->engine->Play();
		}
	}
}

void ApplyEffectiveMute()
{
	const size_t audio = PickAudioInstance();
	for (size_t i = 0; i < s_engines.size(); ++i)
	{
		if (s_engines[i])
		{
			ApplyMuteToInstance(*s_engines[i], i == audio);
		}
	}
}

HRESULT PlayWallpaperPath(PCWSTR path)
{
	if (!path || !*path)
	{
		return E_INVALIDARG;
	}
	s_currentPath = path;

	HRESULT firstErr = S_OK;
	for (auto& inst : s_engines)
	{
		if (!inst || !inst->engine)
		{
			continue;
		}
		// Re-pointing SetSource reloads the pipeline; the engine releases the
		// previous decoder / presenter for us (no manual stop required).
		HRESULT hr = SetEngineSource(inst->engine.Get(), path);
		if (FAILED(hr))
		{
			if (SUCCEEDED(firstErr))
			{
				firstErr = hr;
			}
			continue;
		}
		inst->engine->SetLoop(TRUE);
		// Force mute re-apply to the fresh pipeline; CANPLAY restarts playback
		// and LOADEDMETADATA re-applies the crop for the new clip's dimensions.
		inst->lastMuteApplied = -1;
	}

	ApplyEffectiveMute();
	return firstErr;
}

void RestartWallpaper()
{
	for (auto& inst : s_engines)
	{
		if (inst && inst->engine)
		{
			inst->engine->SetCurrentTime(0.0);
		}
	}
}

void ApplyAspectMode()
{
	for (auto& inst : s_engines)
	{
		if (inst)
		{
			ApplyAspectToInstance(*inst);
		}
	}
}
