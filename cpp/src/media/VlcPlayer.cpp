#include "media/VlcPlayer.h"

#include "app/App.h"
#include "media/VlcHandles.h"
#include "util/Utf8.h"

#include <string>
#include <utility>

HRESULT InitVlc(HWND hwnd, PCWSTR initialPath)
{
	// --no-video-title-show: don't draw the filename overlay on top of the wallpaper.
	// --aout=mmdevice:        force WASAPI shared output. Without this, libvlc may inherit
	//                         a broken/muted audio module choice from %APPDATA%\vlc\vlcrc
	//                         (e.g. if the user has VLC installed and muted there), which
	//                         is the most common reason video plays but audio is silent.
	// We deliberately do NOT pass --no-config because some libvlc builds then refuse to
	// load any audio output module; instead we explicitly override the offending settings.
	// Performance-tuned argument set:
	//   * --avcodec-hw=d3d11va: hardware-decode H.264/HEVC on the GPU. Without this libvlc
	//     may software-decode a 1080p/4K wallpaper, pinning a CPU core and starving the
	//     desktop input thread (visible as cursor lag).
	//   * --vout=direct3d11: explicit GPU video output, avoids GDI/legacy paths.
	//   * --no-osd / --no-stats / --no-snapshot-preview / --no-spu: disable overlays we
	//     never show; each one is a per-frame cost.
	//   * --aout=mmdevice: WASAPI shared output (see note below on muted vlcrc).
	//   * --no-video-title-show: don't draw the filename overlay on top of the wallpaper.
	// We deliberately do NOT pass --no-config because some libvlc builds then refuse to
	// load any audio output module; instead we explicitly override the offending settings.
	const char* vlcArgs[] =
	{
		"--avcodec-hw=d3d11va",
		"--vout=direct3d11",
		"--aout=mmdevice",
		"--no-video-title-show",
		"--no-osd",
		"--no-stats",
		"--no-snapshot-preview",
		"--no-spu",
		"--no-sub-autodetect-file",
		"--quiet",
	};

	// Build everything into local RAII handles. If any step fails, the locals
	// are released automatically and the globals remain untouched.
	VlcInstance vlc(libvlc_new(static_cast<int>(ARRAYSIZE(vlcArgs)), vlcArgs));
	if (!vlc)
	{
		return E_FAIL;
	}

	std::string utf8Path;
	if (HRESULT hr = WideToUtf8(initialPath, utf8Path); FAILED(hr))
	{
		return hr;
	}

	VlcMedia media(libvlc_media_new_path(vlc.get(), utf8Path.c_str()));
	if (!media)
	{
		return E_FAIL;
	}

	VlcMediaList mediaList(libvlc_media_list_new(vlc.get()));
	if (!mediaList)
	{
		return E_FAIL;
	}

	libvlc_media_list_add_media(mediaList.get(), media.get());

	VlcMediaPlayer player(libvlc_media_player_new(vlc.get()));
	if (!player)
	{
		return E_FAIL;
	}

	libvlc_media_player_set_hwnd(player.get(), hwnd);

	VlcMediaListPlayer listPlayer(libvlc_media_list_player_new(vlc.get()));
	if (!listPlayer)
	{
		return E_FAIL;
	}

	libvlc_media_list_player_set_media_player(listPlayer.get(), player.get());
	libvlc_media_list_player_set_media_list(listPlayer.get(), mediaList.get());
	libvlc_media_list_player_set_playback_mode(listPlayer.get(), libvlc_playback_mode_loop);
	libvlc_media_list_player_play(listPlayer.get());
	libvlc_audio_set_volume(player.get(), 100);
	libvlc_audio_set_mute(player.get(), (g_muted || g_autoMuted) ? 1 : 0);

	// Commit: move locals into globals.
	g_vlc        = std::move(vlc);
	g_media      = std::move(media);
	g_mediaList  = std::move(mediaList);
	g_player     = std::move(player);
	g_listPlayer = std::move(listPlayer);
	return S_OK;
}

void ShutdownVlc()
{
	// Release in dependency order: list player wraps player; both wrap media list/media;
	// everything depends on the libvlc instance.
	g_listPlayer.reset();
	g_player.reset();
	g_mediaList.reset();
	g_media.reset();
	g_vlc.reset();
}

void PauseWallpaper()
{
	if (!g_listPlayer)
	{
		return;
	}
	// set_pause(1) is a no-op if already paused; preferred over toggle so we
	// don't accidentally resume on a spurious call.
	libvlc_media_list_player_set_pause(g_listPlayer.get(), 1);
}

void ResumeWallpaper()
{
	if (!g_listPlayer)
	{
		return;
	}
	libvlc_media_list_player_set_pause(g_listPlayer.get(), 0);
}

void ApplyEffectiveMute()
{
	if (!g_player)
	{
		return;
	}
	const bool effective = g_muted || g_autoMuted;
	// Cache last-applied value: libvlc_audio_set_mute can synchronously poke
	// the audio output module, and this is called from a WinEvent hook on the
	// UI thread, so redundant calls show up as cursor stutter.
	static int s_lastApplied = -1;
	const int desired = effective ? 1 : 0;
	if (s_lastApplied == desired)
	{
		return;
	}
	libvlc_audio_set_mute(g_player.get(), desired);
	s_lastApplied = desired;
}

HRESULT PlayWallpaperPath(PCWSTR path)
{
	if (!g_vlc || !g_player || !g_listPlayer || !path || !*path)
	{
		return E_INVALIDARG;
	}

	std::string utf8Path;
	if (HRESULT hr = WideToUtf8(path, utf8Path); FAILED(hr))
	{
		return hr;
	}

	VlcMedia newMedia(libvlc_media_new_path(g_vlc.get(), utf8Path.c_str()));
	if (!newMedia)
	{
		return E_FAIL;
	}

	VlcMediaList newMediaList(libvlc_media_list_new(g_vlc.get()));
	if (!newMediaList)
	{
		return E_FAIL;
	}

	libvlc_media_list_add_media(newMediaList.get(), newMedia.get());

	// Tear down the previous playback synchronously before starting the new one.
	//
	// libvlc_media_list_player_stop only *schedules* a stop on the list-player's
	// internal thread and returns immediately. If we call set_media_list / play
	// right away, the underlying media_player has not yet transitioned to
	// Stopped, so its D3D11VA decoder pool, direct3d11 vout surfaces, and audio
	// output are still alive when libvlc allocates a brand new decoder + vout
	// for the next clip. The orphaned resources are never reclaimed for the
	// lifetime of g_player, which is what makes RSS jump by ~100-200 MB on
	// every wallpaper change.
	//
	// libvlc_media_player_stop (VLC 3.x) is synchronous: it waits for the
	// state machine to reach Stopped, at which point the decoder pool, vout,
	// and aout are torn down. We also explicitly clear the player's internal
	// media reference so the previous libvlc_media_t is released immediately
	// instead of lingering until the next implicit set_media.
	libvlc_media_list_player_stop(g_listPlayer.get());
	libvlc_media_player_stop(g_player.get());
	libvlc_media_player_set_media(g_player.get(), nullptr);

	libvlc_media_list_player_set_media_list(g_listPlayer.get(), newMediaList.get());
	libvlc_media_list_player_set_playback_mode(g_listPlayer.get(), libvlc_playback_mode_loop);
	libvlc_media_list_player_play(g_listPlayer.get());

	libvlc_audio_set_volume(g_player.get(), 100);
	libvlc_audio_set_mute(g_player.get(), (g_muted || g_autoMuted) ? 1 : 0);

	// Replace globals; old objects are released by the unique_ptr destructors.
	g_mediaList = std::move(newMediaList);
	g_media     = std::move(newMedia);
	return S_OK;
}
