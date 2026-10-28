#pragma once
#include <windows.h>
#include <string>
#include "app_state.h"

#define IDI_APP_ICON 101
#define WM_APP_TRAY_NOTIFY (WM_USER + 102)

// Tray Context Menu Command IDs
#define IDM_TRAY_OPEN         40001
#define IDM_TRAY_QUEST_STATUS 40002
#define IDM_TRAY_COMPLETE_ALL 40003
#define IDM_TRAY_STOP_QUEST   40004
#define IDM_TRAY_THEME_TOGGLE 40005
#define IDM_TRAY_INSTALL_APP  40006
#define IDM_TRAY_EXIT         40007

namespace ShellIntegration {
    inline constexpr const wchar_t* kAppUserModelId = L"Nixlabs.DiscordHelper.Desktop";

    // Application identity, DPI awareness, and single-instance activation
    void InitializeProcessIdentity();
    bool ActivateExistingInstanceIfRunning();

    // Multi-resolution DPI-aware icon loading & assignment (WM_SETICON ICON_SMALL / ICON_BIG)
    void UpdateWindowIconsForDpi(HWND hwnd, HINSTANCE hInstance, UINT dpi);
    HICON CreateBrandIconDIB(int size);

    // Windows 11 DWM Mica / Mica Alt, Rounded Corners, Immersive Dark Mode, Border & System Accent Color
    void ApplyWindows11FrameAttributes(HWND hwnd, bool darkMode);
    bool QuerySystemUsesDarkTheme();
    UINT32 QuerySystemAccentColor(bool darkMode);
    void SyncSystemThemeAndAccent(AppState& state);

    // Notification Area (System Tray) Icon, Dynamic Quest Tooltip, and Native Context Menu
    void InitializeTrayIcon(HWND hwnd, HINSTANCE hInstance);
    void UpdateTrayIconState(HWND hwnd, const AppState& state);
    void RemoveTrayIcon(HWND hwnd);
    void ShowTrayContextMenu(HWND hwnd, AppState& state);

    // Native Windows System Notifications (NIF_INFO | NIIF_USER | NIF_REALTIME)
    void ShowNativeNotification(HWND hwnd, const wchar_t* title, const wchar_t* message, DWORD infoFlags = 0x00000004 /* NIIF_USER */);

    // Windows 11 Taskbar Progress Bar (ITaskbarList3)
    void InitializeTaskbarList(HWND hwnd);
    void UpdateTaskbarProgress(HWND hwnd, const AppState& state);
    void ReleaseTaskbarList();

    // High-quality Start Menu Shortcut (.lnk with PKEY_AppUserModel_ID) & Installed Apps Registration
    bool RegisterStartMenuAndInstalledApp(HWND hwnd, bool showFeedback);
    bool UnregisterStartMenuAndInstalledApp();
}
