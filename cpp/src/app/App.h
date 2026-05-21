#pragma once

#include <windows.h>
#include <shellapi.h>
#include <vlc/vlc.h>

#include "media/VlcHandles.h"

// -----------------------------------------------------------------------------
// Tray / menu identifiers
// -----------------------------------------------------------------------------
constexpr UINT WM_TRAYICON              = WM_USER + 1;
constexpr UINT ID_TRAY_EXIT             = 1001;
constexpr UINT ID_TRAY_CHANGE_WALLPAPER = 1002;
constexpr UINT ID_TRAY_MUTE             = 1003;

// -----------------------------------------------------------------------------
// Shared application state
//
// Step 5 of the refactor only splits files. Globals are kept here as `extern`
// declarations and defined in App.cpp so every module can see the same state.
// A later refactor step (3) will fold these into a single AppState class.
// -----------------------------------------------------------------------------
extern HWND g_workerw;
extern HWND g_shellViewHost;
extern HWND g_shellDefView;
extern HWND g_progman;
extern bool g_raisedDesktop;

extern VlcInstance        g_vlc;
extern VlcMediaListPlayer g_listPlayer;
extern VlcMediaList       g_mediaList;
extern VlcMediaPlayer     g_player;
extern VlcMedia           g_media;

extern bool g_muted;

// Main window procedure (defined in App.cpp).
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
