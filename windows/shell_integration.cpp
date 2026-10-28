#include "shell_integration.h"
#include "api_client.h"
#include <dwmapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propsys.h>
#include <propvarutil.h>
#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <cwchar>

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif

namespace ShellIntegration {

static HICON s_hIconSmall = nullptr;
static HICON s_hIconBig = nullptr;
static HICON s_hTrayIcon = nullptr;
static bool s_trayAdded = false;
static ITaskbarList3* s_taskbarList = nullptr;

// Undocumented uxtheme.dll ordinals (135 = SetPreferredAppMode, 133 = AllowDarkModeForWindow, 136 = FlushMenuThemes)
// used by Windows 11 inbox Win32 apps (Notepad, Task Manager) to render dark native HMENU context menus.
enum PreferredAppMode {
    PAM_Default = 0,
    PAM_AllowDark = 1,
    PAM_ForceDark = 2,
    PAM_ForceLight = 3
};
using fnSetPreferredAppMode = PreferredAppMode(WINAPI*)(PreferredAppMode appMode);
using fnAllowDarkModeForWindow = bool(WINAPI*)(HWND hWnd, bool allow);
using fnFlushMenuThemes = void(WINAPI*)();

static fnSetPreferredAppMode s_pSetPreferredAppMode = nullptr;
static fnAllowDarkModeForWindow s_pAllowDarkModeForWindow = nullptr;
static fnFlushMenuThemes s_pFlushMenuThemes = nullptr;

static void InitUxThemeDarkMenus() {
    static bool s_initialized = false;
    if (s_initialized) return;
    s_initialized = true;
    HMODULE hUxtheme = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (hUxtheme) {
        s_pSetPreferredAppMode = reinterpret_cast<fnSetPreferredAppMode>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(135)));
        s_pAllowDarkModeForWindow = reinterpret_cast<fnAllowDarkModeForWindow>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(133)));
        s_pFlushMenuThemes = reinterpret_cast<fnFlushMenuThemes>(GetProcAddress(hUxtheme, MAKEINTRESOURCEA(136)));
    }
}

void InitializeProcessIdentity() {
    // Enable Per-Monitor V2 DPI Awareness programmatically before any HWND creation (fallback to manifest)
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    // Assign explicit Application User Model ID before presenting UI or manipulating Jump Lists / shortcuts
    SetCurrentProcessExplicitAppUserModelID(kAppUserModelId);

    // Initialize STA COM for ITaskbarList3 and IShellLinkW
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

    InitUxThemeDarkMenus();
    if (s_pSetPreferredAppMode) {
        s_pSetPreferredAppMode(PAM_AllowDark);
    }
}

bool ActivateExistingInstanceIfRunning() {
    HANDLE hMutex = CreateMutexW(nullptr, FALSE, L"Local\\NixlabsDiscordHelper_SingleInstance_Mutex");
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"NixlabsDiscordHelperWnd", nullptr);
        if (hExisting) {
            if (!IsWindowVisible(hExisting)) {
                ShowWindow(hExisting, SW_SHOW);
            }
            if (IsIconic(hExisting)) {
                ShowWindow(hExisting, SW_RESTORE);
            }
            SetForegroundWindow(hExisting);
        }
        CloseHandle(hMutex);
        return true;
    }
    return false;
}

// Synthesize the exact nixlabs brand icon (assets/nixlabs-discord-helper.svg: #101010 squircle + #f7f4eb 'n' glyph)
// with 4x4 supersampled anti-aliasing as an in-memory fallback if resource loading ever fails.
HICON CreateBrandIconDIB(int size) {
    BITMAPV5HEADER bi = {0};
    bi.bV5Size = sizeof(BITMAPV5HEADER);
    bi.bV5Width = size;
    bi.bV5Height = -size;
    bi.bV5Planes = 1;
    bi.bV5BitCount = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask = 0x00FF0000;
    bi.bV5GreenMask = 0x0000FF00;
    bi.bV5BlueMask = 0x000000FF;
    bi.bV5AlphaMask = 0xFF000000;

    void* pvBits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hColor = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS, &pvBits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hColor || !pvBits) return nullptr;

    UINT32* pixels = static_cast<UINT32*>(pvBits);
    const float scale = 32.0f / static_cast<float>(size);

    auto insideSquircle = [](float ux, float uy, float rad) -> bool {
        float dx = (ux < rad) ? (rad - ux) : (ux > 32.0f - rad ? ux - (32.0f - rad) : 0.0f);
        float dy = (uy < rad) ? (rad - uy) : (uy > 32.0f - rad ? uy - (32.0f - rad) : 0.0f);
        return (dx * dx + dy * dy) <= (rad * rad);
    };

    auto insideGlyphN = [](float ux, float uy) -> bool {
        if (uy < 8.5f || uy > 24.0f || ux < 8.0f || ux > 24.0f) return false;
        // Left stem: x in [8, 12], y in [9, 24]
        if (ux >= 8.0f && ux <= 12.0f && uy >= 9.0f && uy <= 24.0f) return true;
        // Right stem: x in [20, 24], y in [16, 24]
        if (ux >= 20.0f && ux <= 24.0f && uy >= 16.0f && uy <= 24.0f) return true;
        // Top arch outer circle centered at (16.5, 16.0) with r=7.5, inner cutout centered at (16.0, 16.5) with r=4.0
        if (uy <= 16.5f) {
            float odx = (ux - 16.5f) / 7.5f;
            float ody = (uy - 16.0f) / 7.5f;
            float idx = (ux - 16.0f) / 4.0f;
            float idy = (uy - 16.5f) / 4.0f;
            if ((odx * odx + ody * ody <= 1.0f) && (idx * idx + idy * idy >= 1.0f)) {
                return true;
            }
        }
        return false;
    };

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            int bgHits = 0;
            int fgHits = 0;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    float ux = (x + (sx + 0.5f) * 0.25f) * scale;
                    float uy = (y + (sy + 0.5f) * 0.25f) * scale;
                    if (insideSquircle(ux, uy, 8.0f)) {
                        bgHits++;
                        if (insideGlyphN(ux, uy)) fgHits++;
                    }
                }
            }
            if (bgHits == 0) {
                pixels[y * size + x] = 0x00000000;
            } else {
                float alpha = bgHits / 16.0f;
                float fgRatio = fgHits / static_cast<float>(bgHits);
                int r = static_cast<int>((16.0f * (1.0f - fgRatio) + 247.0f * fgRatio) * alpha);
                int g = static_cast<int>((16.0f * (1.0f - fgRatio) + 244.0f * fgRatio) * alpha);
                int b = static_cast<int>((16.0f * (1.0f - fgRatio) + 235.0f * fgRatio) * alpha);
                int a = static_cast<int>(255.0f * alpha);
                pixels[y * size + x] = (a << 24) | (r << 16) | (g << 8) | b;
            }
        }
    }

    HBITMAP hMask = CreateBitmap(size, size, 1, 1, nullptr);
    ICONINFO ii = {TRUE, 0, 0, hMask, hColor};
    HICON hIcon = CreateIconIndirect(&ii);
    DeleteObject(hColor);
    DeleteObject(hMask);
    return hIcon;
}

static HICON LoadScaledAppIcon(HINSTANCE hInstance, int cx, int cy) {
    HICON hIcon = static_cast<HICON>(LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, cx, cy, LR_DEFAULTCOLOR));
    if (!hIcon) {
        hIcon = CreateBrandIconDIB(cx);
    }
    return hIcon;
}

void UpdateWindowIconsForDpi(HWND hwnd, HINSTANCE hInstance, UINT dpi) {
    if (dpi == 0) dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;

    int cxSmall = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    int cySmall = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    int cxBig = GetSystemMetricsForDpi(SM_CXICON, dpi);
    int cyBig = GetSystemMetricsForDpi(SM_CYICON, dpi);

    HICON newSmall = LoadScaledAppIcon(hInstance, cxSmall, cySmall);
    HICON newBig = LoadScaledAppIcon(hInstance, cxBig, cyBig);

    if (newSmall) {
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(newSmall));
        if (s_hIconSmall && s_hIconSmall != newSmall) DestroyIcon(s_hIconSmall);
        s_hIconSmall = newSmall;
    }
    if (newBig) {
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(newBig));
        if (s_hIconBig && s_hIconBig != newBig) DestroyIcon(s_hIconBig);
        s_hIconBig = newBig;
    }
}

bool QuerySystemUsesDarkTheme() {
    DWORD useLight = 0;
    DWORD cbSize = sizeof(useLight);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme",
                     RRF_RT_REG_DWORD, nullptr, &useLight, &cbSize) == ERROR_SUCCESS) {
        return (useLight == 0);
    }
    return true;
}

static UINT32 AdjustLuminanceForContrast(UINT32 rgb, bool darkMode) {
    int r = (rgb >> 16) & 0xFF;
    int g = (rgb >> 8) & 0xFF;
    int b = rgb & 0xFF;
    if (r == 0 && g == 0 && b == 0) {
        return darkMode ? 0x70B5FF : 0x0B57C3;
    }
    if (darkMode) {
        // Lighten toward white by 42% so buttons & focus rings have crisp contrast on dark Mica surfaces
        r = r + ((255 - r) * 42) / 100;
        g = g + ((255 - g) * 42) / 100;
        b = b + ((255 - b) * 42) / 100;
    } else {
        // Darken by 15% for readable contrast on light surfaces
        r = (r * 85) / 100;
        g = (g * 85) / 100;
        b = (b * 85) / 100;
    }
    return ((r & 0xFF) << 16) | ((g & 0xFF) << 8) | (b & 0xFF);
}

UINT32 QuerySystemAccentColor(bool darkMode) {
    DWORD argb = 0;
    BOOL opaque = FALSE;
    if (SUCCEEDED(DwmGetColorizationColor(&argb, &opaque)) && (argb & 0x00FFFFFF) != 0) {
        return AdjustLuminanceForContrast(argb & 0x00FFFFFF, darkMode);
    }
    DWORD abgr = 0;
    DWORD cbSize = sizeof(abgr);
    if (RegGetValueW(HKEY_CURRENT_USER,
                     L"Software\\Microsoft\\Windows\\DWM",
                     L"AccentColor",
                     RRF_RT_REG_DWORD, nullptr, &abgr, &cbSize) == ERROR_SUCCESS && (abgr & 0x00FFFFFF) != 0) {
        UINT32 rgb = ((abgr & 0xFF) << 16) | (abgr & 0xFF00) | ((abgr >> 16) & 0xFF);
        return AdjustLuminanceForContrast(rgb, darkMode);
    }
    return darkMode ? 0xA8CDFF : 0x0B57C3;
}

void SyncSystemThemeAndAccent(AppState& state) {
    bool sysDark = QuerySystemUsesDarkTheme();
    std::lock_guard<std::mutex> lk(state.mu);
    if (!state.userOverrodeTheme) {
        state.darkMode = sysDark;
    }
    state.accentColor = QuerySystemAccentColor(state.darkMode);
}

void ApplyWindows11FrameAttributes(HWND hwnd, bool darkMode) {
    if (!hwnd) return;

    BOOL useDark = darkMode ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &useDark, sizeof(useDark));

    int cornerPref = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPref, sizeof(cornerPref));

    int backdropType = DWMSBT_MAINWINDOW; // Mica backdrop behind entire window including non-client title bar
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropType, sizeof(backdropType));

    // Extend DWM frame across client area so Direct2D transparent clear exposes real Windows 11 Mica
    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);

    InitUxThemeDarkMenus();
    if (s_pSetPreferredAppMode) {
        s_pSetPreferredAppMode(darkMode ? PAM_ForceDark : PAM_ForceLight);
    }
    if (s_pAllowDarkModeForWindow) {
        s_pAllowDarkModeForWindow(hwnd, darkMode);
    }
    if (s_pFlushMenuThemes) {
        s_pFlushMenuThemes();
    }
}

void InitializeTrayIcon(HWND hwnd, HINSTANCE hInstance) {
    if (s_trayAdded || !hwnd) return;
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    int cx = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
    int cy = GetSystemMetricsForDpi(SM_CYSMICON, dpi);
    s_hTrayIcon = LoadScaledAppIcon(hInstance, cx, cy);

    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
    nid.uCallbackMessage = WM_APP_TRAY_NOTIFY;
    nid.hIcon = s_hTrayIcon;
    wcscpy_s(nid.szTip, L"nixlabs | Discord Helper\nIdle — Ready to complete quests");

    if (Shell_NotifyIconW(NIM_ADD, &nid)) {
        nid.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid);
        s_trayAdded = true;
    }
}

void UpdateTrayIconState(HWND hwnd, const AppState& state) {
    if (!s_trayAdded || !hwnd) return;

    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_TIP | NIF_SHOWTIP;

    if (state.activeProgress.running) {
        swprintf_s(nid.szTip, L"nixlabs | Discord Helper\nActive: %s (%.0f%%)",
                   state.activeProgress.questName.c_str(), state.activeProgress.percent);
    } else if (state.activeProgress.completed && !state.activeProgress.questName.empty()) {
        swprintf_s(nid.szTip, L"nixlabs | Discord Helper\nCompleted: %s",
                   state.activeProgress.questName.c_str());
    } else {
        wcscpy_s(nid.szTip, L"nixlabs | Discord Helper\nIdle — Ready to complete quests");
    }

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void RemoveTrayIcon(HWND hwnd) {
    if (s_trayAdded && hwnd) {
        NOTIFYICONDATAW nid = {0};
        nid.cbSize = sizeof(NOTIFYICONDATAW);
        nid.hWnd = hwnd;
        nid.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        s_trayAdded = false;
    }
    if (s_hTrayIcon) {
        DestroyIcon(s_hTrayIcon);
        s_hTrayIcon = nullptr;
    }
    if (s_hIconSmall) {
        DestroyIcon(s_hIconSmall);
        s_hIconSmall = nullptr;
    }
    if (s_hIconBig) {
        DestroyIcon(s_hIconBig);
        s_hIconBig = nullptr;
    }
}

void ShowNativeNotification(HWND hwnd, const wchar_t* title, const wchar_t* message, DWORD infoFlags) {
    if (!s_trayAdded || !hwnd || !title || !message) return;

    NOTIFYICONDATAW nid = {0};
    nid.cbSize = sizeof(NOTIFYICONDATAW);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_INFO | NIF_SHOWTIP;
    nid.dwInfoFlags = infoFlags | NIIF_RESPECT_QUIET_TIME;
    nid.hBalloonIcon = s_hIconBig ? s_hIconBig : s_hTrayIcon;
    wcsncpy_s(nid.szInfoTitle, title, _TRUNCATE);
    wcsncpy_s(nid.szInfo, message, _TRUNCATE);
    wcscpy_s(nid.szTip, L"nixlabs | Discord Helper");

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void ShowTrayContextMenu(HWND hwnd, AppState& state) {
    HMENU hMenu = CreatePopupMenu();
    if (!hMenu) return;

    std::wstring statusLabel;
    bool isRunning = false;
    bool isDark = true;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        isRunning = state.activeProgress.running;
        isDark = state.darkMode;
        if (isRunning) {
            wchar_t buf[160] = {0};
            swprintf_s(buf, L"Active: %s (%.0f%%)", state.activeProgress.questName.c_str(), state.activeProgress.percent);
            statusLabel = buf;
        } else {
            statusLabel = L"Status: Idle (No active quest)";
        }
    }

    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_OPEN, L"Open Discord Helper");
    SetMenuDefaultItem(hMenu, IDM_TRAY_OPEN, FALSE);
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING | MF_GRAYED, IDM_TRAY_QUEST_STATUS, statusLabel.c_str());

    if (isRunning) {
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_STOP_QUEST, L"Stop Current Quest");
    } else {
        AppendMenuW(hMenu, MF_STRING, IDM_TRAY_COMPLETE_ALL, L"Complete All Eligible Quests");
    }

    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_THEME_TOGGLE, isDark ? L"Switch to Light Theme" : L"Switch to Dark Theme");
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_INSTALL_APP, L"Register Start Menu & Installed App");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(hMenu, MF_STRING, IDM_TRAY_EXIT, L"Exit Discord Helper");

    POINT pt = {0};
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd);
    TrackPopupMenuEx(hMenu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DestroyMenu(hMenu);
}

void InitializeTaskbarList(HWND hwnd) {
    if (s_taskbarList) return;
    if (SUCCEEDED(CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER,
                                   IID_PPV_ARGS(&s_taskbarList)))) {
        if (FAILED(s_taskbarList->HrInit())) {
            s_taskbarList->Release();
            s_taskbarList = nullptr;
        }
    }
    (void)hwnd;
}

void UpdateTaskbarProgress(HWND hwnd, const AppState& state) {
    if (!s_taskbarList || !hwnd) return;

    if (state.activeProgress.running) {
        ULONGLONG pct = static_cast<ULONGLONG>((std::max)(1.0, (std::min)(100.0, state.activeProgress.percent)));
        s_taskbarList->SetProgressState(hwnd, TBPF_NORMAL);
        s_taskbarList->SetProgressValue(hwnd, pct, 100ULL);
    } else if (!state.activeProgress.errorText.empty()) {
        s_taskbarList->SetProgressState(hwnd, TBPF_ERROR);
        s_taskbarList->SetProgressValue(hwnd, 100ULL, 100ULL);
    } else {
        s_taskbarList->SetProgressState(hwnd, TBPF_NOPROGRESS);
    }
}

void ReleaseTaskbarList() {
    if (s_taskbarList) {
        s_taskbarList->Release();
        s_taskbarList = nullptr;
    }
}

// PKEY_AppUserModel_ID = {9F4C2855-9F79-4B39-A8D0-E1D42DE1D5F3}, 5
static const PROPERTYKEY kPKEY_AppUserModel_ID = {
    {0x9F4C2855, 0x9F79, 0x4B39, {0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3}}, 5
};

bool RegisterStartMenuAndInstalledApp(HWND hwnd, bool showFeedback) {
    wchar_t exePath[MAX_PATH] = {0};
    if (!GetModuleFileNameW(nullptr, exePath, MAX_PATH)) return false;

    std::wstring workDir = exePath;
    size_t lastSlash = workDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        workDir = workDir.substr(0, lastSlash);
    }

    // 1. Create Start Menu Shortcut (%APPDATA%\Microsoft\Windows\Start Menu\Programs\Discord Helper.lnk)
    // with explicit PKEY_AppUserModel_ID for proper Windows 11 taskbar grouping, pinning, and notifications.
    wchar_t startMenuDir[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuDir))) {
        std::wstring lnkPath = std::wstring(startMenuDir) + L"\\Discord Helper.lnk";
        IShellLinkW* psl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&psl)))) {
            psl->SetPath(exePath);
            psl->SetWorkingDirectory(workDir.c_str());
            psl->SetDescription(L"Discord Quest Automator and Profile Companion");
            psl->SetIconLocation(exePath, 0);

            IPropertyStore* pps = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&pps)))) {
                PROPVARIANT pv;
                PropVariantInit(&pv);
                pv.vt = VT_LPWSTR;
                pv.pwszVal = const_cast<LPWSTR>(kAppUserModelId);
                pps->SetValue(kPKEY_AppUserModel_ID, pv);
                pps->Commit();
                pps->Release();
            }

            IPersistFile* ppf = nullptr;
            if (SUCCEEDED(psl->QueryInterface(IID_PPV_ARGS(&ppf)))) {
                ppf->Save(lnkPath.c_str(), TRUE);
                ppf->Release();
            }
            psl->Release();
        }
    }

    // 2. Register in HKCU\Software\Microsoft\Windows\CurrentVersion\Uninstall\NixlabsDiscordHelper
    // so the application appears cleanly in Windows 11 Settings -> Apps -> Installed apps with its crisp icon.
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER,
                        L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NixlabsDiscordHelper",
                        0, nullptr, 0, KEY_WRITE, nullptr, &hKey, nullptr) == ERROR_SUCCESS) {
        auto setSz = [&](const wchar_t* name, const std::wstring& val) {
            RegSetValueExW(hKey, name, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(val.c_str()),
                           static_cast<DWORD>((val.size() + 1) * sizeof(wchar_t)));
        };
        auto setDword = [&](const wchar_t* name, DWORD val) {
            RegSetValueExW(hKey, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&val), sizeof(val));
        };

        std::wstring displayIcon = std::wstring(exePath) + L",0";
        std::wstring uninstallCmd = L"\"" + std::wstring(exePath) + L"\" --uninstall";

        setSz(L"DisplayName", L"Nixlabs Discord Helper");
        setSz(L"DisplayVersion", L"1.0.14");
        setSz(L"Publisher", L"Nixlabs");
        setSz(L"DisplayIcon", displayIcon);
        setSz(L"InstallLocation", workDir);
        setSz(L"UninstallString", uninstallCmd);
        setSz(L"URLInfoAbout", L"https://nixlabs.tech");
        setDword(L"NoModify", 1);
        setDword(L"NoRepair", 1);
        setDword(L"EstimatedSize", 1140);
        RegCloseKey(hKey);
    }

    if (showFeedback && hwnd) {
        ShowNativeNotification(hwnd, L"Installed Apps & Start Menu Ready",
                               L"Discord Helper is now registered in the Windows 11 Start menu and Installed Apps.",
                               NIIF_USER);
    }
    return true;
}

bool UnregisterStartMenuAndInstalledApp() {
    wchar_t startMenuDir[MAX_PATH] = {0};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_PROGRAMS, nullptr, 0, startMenuDir))) {
        std::wstring lnkPath = std::wstring(startMenuDir) + L"\\Discord Helper.lnk";
        DeleteFileW(lnkPath.c_str());
    }
    RegDeleteTreeW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\NixlabsDiscordHelper");
    return true;
}

} // namespace ShellIntegration
