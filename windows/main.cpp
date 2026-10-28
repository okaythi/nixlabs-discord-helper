#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cstdio>
#include <cwchar>
#include <cmath>
#include <thread>
#include <algorithm>
#include "app_state.h"
#include "api_client.h"
#include "ui_renderer.h"
#include "shell_integration.h"

#define WM_APP_STATE_UPDATED (WM_USER + 101)
#define TIMER_BENCH_STEP     1
#define TIMER_ANIM_PROGRESS  2

static AppState g_state;
static UiRenderer g_renderer;
static int g_benchPhase = 0;
static bool g_trackingMouseLeave = false;
static bool g_animTimerActive = false;
static bool g_shownCloseToTrayTip = false;

static void CopyToClipboard(HWND hwnd, const std::wstring& text) {
    if (!OpenClipboard(hwnd)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

static std::wstring ReadFromClipboard(HWND hwnd) {
    std::wstring result;
    if (!OpenClipboard(hwnd)) return result;
    HANDLE hData = GetClipboardData(CF_UNICODETEXT);
    if (hData) {
        const wchar_t* pszText = static_cast<const wchar_t*>(GlobalLock(hData));
        if (pszText) {
            result = pszText;
            GlobalUnlock(hData);
        }
    }
    CloseClipboard();
    return result;
}

static void SyncProgressAnimationTimer(HWND hwnd) {
    bool needTimer = false;
    {
        std::lock_guard<std::mutex> lk(g_state.mu);
        float target = static_cast<float>(g_state.activeProgress.percent);
        if (g_state.activeProgress.running || g_state.activeProgress.completed) {
            if (std::fabs(g_state.animatedProgressPct - target) > 0.25f) {
                needTimer = true;
            } else {
                g_state.animatedProgressPct = target;
            }
        } else {
            g_state.animatedProgressPct = 0.0f;
        }
    }
    if (needTimer && !g_animTimerActive) {
        SetTimer(hwnd, TIMER_ANIM_PROGRESS, 16, nullptr);
        g_animTimerActive = true;
    } else if (!needTimer && g_animTimerActive) {
        KillTimer(hwnd, TIMER_ANIM_PROGRESS);
        g_animTimerActive = false;
    }
}

static void RestoreAndActivateWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static int FindHitTargetAt(int x, int y) {
    std::lock_guard<std::mutex> lk(g_state.mu);
    for (auto it = g_state.clickTargets.rbegin(); it != g_state.clickTargets.rend(); ++it) {
        if (x >= it->rc.left && x <= it->rc.right && y >= it->rc.top && y <= it->rc.bottom) {
            return it->id;
        }
    }
    return HIT_NONE;
}

static void HandleClick(HWND hwnd, int x, int y) {
    int hitId = FindHitTargetAt(x, y);

    // Clicking outside the open locale flyout closes it cleanly
    if (g_state.isLocaleMenuOpen && hitId != HIT_LOCALE_CYCLE &&
        !(hitId >= HIT_LOCALE_OPTION_BASE && hitId < HIT_LOCALE_OPTION_BASE + 6)) {
        g_state.isLocaleMenuOpen = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        if (hitId == HIT_NONE) return;
    }

    if (hitId == HIT_NONE) {
        if (g_state.focusedInput != FocusedInput::None) {
            g_state.focusedInput = FocusedInput::None;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return;
    }

    if (hitId == HIT_THEME_TOGGLE) {
        g_state.userOverrodeTheme = true;
        g_state.darkMode = !g_state.darkMode;
        g_state.accentColor = ShellIntegration::QuerySystemAccentColor(g_state.darkMode);
        ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
        ApiClient::SaveAuthToDisk(g_state);
    } else if (hitId == HIT_LOCALE_CYCLE) {
        g_state.isLocaleMenuOpen = !g_state.isLocaleMenuOpen;
    } else if (hitId >= HIT_LOCALE_OPTION_BASE && hitId < HIT_LOCALE_OPTION_BASE + 6) {
        g_state.locale = static_cast<LocaleId>(hitId - HIT_LOCALE_OPTION_BASE);
        g_state.isLocaleMenuOpen = false;
        ApiClient::SaveAuthToDisk(g_state);
    } else if (hitId == HIT_LOGOUT) {
        ApiClient::PerformLogout(g_state);
    } else if (hitId == HIT_TAB_ACCOUNT) {
        g_state.activeTab = NavTab::Account;
        g_state.scrollY = 0;
    } else if (hitId == HIT_TAB_QUESTS) {
        g_state.activeTab = NavTab::Quests;
        g_state.scrollY = 0;
        ApiClient::FetchQuestsList(g_state, false);
    } else if (hitId == HIT_TAB_SNOWFLAKES) {
        g_state.activeTab = NavTab::Snowflakes;
        g_state.scrollY = 0;
    } else if (hitId == HIT_ECOSYSTEM_LINK) {
        ShellExecuteW(nullptr, L"open", L"https://nixlabs.tech", nullptr, nullptr, SW_SHOWNORMAL);
    } else if (hitId == HIT_LOGIN_SHOW_FORM) {
        g_state.loginMode = LoginMode::InAppForm;
        g_state.focusedInput = FocusedInput::LoginIdentifier;
    } else if (hitId == HIT_LOGIN_CREATE_ACCOUNT) {
        ApiClient::OpenCreateAccountInBrowser(g_state);
    } else if (hitId == HIT_LOGIN_IDENTIFIER_INPUT) {
        g_state.focusedInput = FocusedInput::LoginIdentifier;
    } else if (hitId == HIT_LOGIN_PASSWORD_INPUT) {
        g_state.focusedInput = FocusedInput::LoginPassword;
    } else if (hitId == HIT_LOGIN_SUBMIT) {
        ApiClient::PerformLogin(g_state, g_state.loginIdentifier, g_state.loginPassword);
    } else if (hitId == HIT_LOGIN_BACK) {
        g_state.loginMode = LoginMode::Card;
    } else if (hitId == HIT_DISCORD_CONNECT_BTN) {
        g_state.isModalOpen = true;
    } else if (hitId == HIT_DISCORD_DISCONNECT_BTN) {
        ApiClient::DisconnectDiscordAccount(g_state);
    } else if (hitId == HIT_MODAL_CLOSE) {
        g_state.isModalOpen = false;
    } else if (hitId == HIT_MODAL_TAB_CREDENTIALS) {
        g_state.modalTab = ModalTab::Credentials;
    } else if (hitId == HIT_MODAL_TAB_TOKEN) {
        g_state.modalTab = ModalTab::Token;
        g_state.focusedInput = FocusedInput::ModalToken;
    } else if (hitId == HIT_MODAL_TOKEN_INPUT) {
        g_state.focusedInput = FocusedInput::ModalToken;
    } else if (hitId == HIT_MODAL_SUBMIT) {
        if (g_state.modalTab == ModalTab::Token) ApiClient::SubmitDiscordToken(g_state);
        else ApiClient::SubmitDiscordCredentials(g_state);
    } else if (hitId == HIT_QUESTS_REFRESH) {
        ApiClient::FetchQuestsList(g_state, true);
    } else if (hitId == HIT_QUESTS_COMPLETE_ALL) {
        ApiClient::StartAllQuests(g_state);
    } else if (hitId == HIT_QUESTS_STOP) {
        ApiClient::CancelRunningQuests(g_state);
        ShellIntegration::UpdateTaskbarProgress(hwnd, g_state);
        ShellIntegration::UpdateTrayIconState(hwnd, g_state);
    } else if (hitId == HIT_QUESTS_LOAD_MORE) {
        g_state.visibleQuestCount += 12;
    } else if (hitId == HIT_SNOWFLAKE_INPUT) {
        g_state.focusedInput = FocusedInput::SnowflakeId;
    } else if (hitId >= HIT_SNOWFLAKE_COPY_BASE && hitId < HIT_SNOWFLAKE_COPY_BASE + 6) {
        CopyToClipboard(hwnd, g_state.snowflakeDecoded.rawId);
    } else if (hitId >= HIT_QUEST_START_BASE && hitId < HIT_QUEST_START_BASE + 500) {
        int idx = hitId - HIT_QUEST_START_BASE;
        if (idx >= 0 && idx < (int)g_state.quests.size()) {
            ApiClient::StartSingleQuest(g_state, g_state.quests[idx].id);
        }
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (wParam != SIZE_MINIMIZED) {
            g_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_DPICHANGED: {
        UINT newDpi = HIWORD(wParam);
        g_state.dpiScale = (newDpi > 0) ? ((float)newDpi / 96.0f) : 1.0f;
        g_renderer.SetDpi(newDpi);
        ShellIntegration::UpdateWindowIconsForDpi(hwnd, GetModuleHandleW(nullptr), newDpi);
        const RECT* prcNewWindow = reinterpret_cast<const RECT*>(lParam);
        if (prcNewWindow) {
            SetWindowPos(hwnd, nullptr,
                         prcNewWindow->left,
                         prcNewWindow->top,
                         prcNewWindow->right - prcNewWindow->left,
                         prcNewWindow->bottom - prcNewWindow->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO: {
        // Windows 11 Snap Layouts requirement: minimum width <= 500 epx (we use 360 epx scaled by DPI)
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) dpi = 96;
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = MulDiv(360, (int)dpi, 96);
        mmi->ptMinTrackSize.y = MulDiv(380, (int)dpi, 96);
        return 0;
    }

    case WM_ACTIVATE: {
        bool active = (LOWORD(wParam) != WA_INACTIVE);
        if (g_state.isWindowActive != active) {
            g_state.isWindowActive = active;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_SETTINGCHANGE:
    case WM_DWMCOLORIZATIONCOLORCHANGED: {
        ShellIntegration::SyncSystemThemeAndAccent(g_state);
        ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_POWERBROADCAST: {
        if (wParam == PBT_APMSUSPEND) {
            // Gracefully flush any dirty state before Windows enters modern standby / sleep
            ApiClient::FlushStateIfDirty(g_state);
        } else if (wParam == PBT_APMRESUMEAUTOMATIC || wParam == PBT_APMRESUMESUSPEND) {
            // Restore DWM backdrop and repaint cleanly on wake
            ShellIntegration::SyncSystemThemeAndAccent(g_state);
            ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return TRUE;
    }

    case WM_QUERYENDSESSION:
        ApiClient::FlushStateIfDirty(g_state);
        return TRUE;

    case WM_ENDSESSION:
        if (wParam) {
            ApiClient::FlushStateIfDirty(g_state);
            ShellIntegration::RemoveTrayIcon(hwnd);
        }
        return 0;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        g_renderer.Render(g_state);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_MOUSEMOVE: {
        if (!g_trackingMouseLeave) {
            TRACKMOUSEEVENT tme = {sizeof(TRACKMOUSEEVENT), TME_LEAVE, hwnd, 0};
            TrackMouseEvent(&tme);
            g_trackingMouseLeave = true;
        }
        int hitId = FindHitTargetAt(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (hitId == HIT_LOGIN_IDENTIFIER_INPUT || hitId == HIT_LOGIN_PASSWORD_INPUT ||
            hitId == HIT_SNOWFLAKE_INPUT || hitId == HIT_MODAL_TOKEN_INPUT) {
            SetCursor(LoadCursorW(nullptr, IDC_IBEAM));
        } else if (hitId != HIT_NONE) {
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
        } else {
            SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        }
        if (g_state.hoveredTargetId != hitId) {
            g_state.hoveredTargetId = hitId;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_MOUSELEAVE: {
        g_trackingMouseLeave = false;
        if (g_state.hoveredTargetId != HIT_NONE) {
            g_state.hoveredTargetId = HIT_NONE;
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_LBUTTONDOWN:
        HandleClick(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;

    case WM_MOUSEWHEEL: {
        if (GetKeyState(VK_CONTROL) & 0x8000) return 0; // Eat Ctrl+Wheel zoom
        int delta = GET_WHEEL_DELTA_WPARAM(wParam);
        g_state.scrollY = (std::max)(0, (std::min)(g_state.maxScrollY, g_state.scrollY - (delta / 2)));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_KEYDOWN: {
        if (wParam == VK_ESCAPE) {
            if (g_state.isLocaleMenuOpen) {
                g_state.isLocaleMenuOpen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_state.isModalOpen) {
                g_state.isModalOpen = false;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_state.focusedInput != FocusedInput::None) {
                g_state.focusedInput = FocusedInput::None;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        } else if (wParam == VK_TAB) {
            if (g_state.loginMode == LoginMode::InAppForm && !g_state.isAuthenticated) {
                g_state.focusedInput = (g_state.focusedInput == FocusedInput::LoginIdentifier)
                    ? FocusedInput::LoginPassword
                    : FocusedInput::LoginIdentifier;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
        } else if (wParam == VK_RETURN) {
            if (g_state.isModalOpen) {
                if (g_state.modalTab == ModalTab::Token) ApiClient::SubmitDiscordToken(g_state);
                else ApiClient::SubmitDiscordCredentials(g_state);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (g_state.focusedInput == FocusedInput::LoginIdentifier ||
                g_state.focusedInput == FocusedInput::LoginPassword) {
                ApiClient::PerformLogin(g_state, g_state.loginIdentifier, g_state.loginPassword);
                return 0;
            }
        } else if (wParam == 'V' && (GetKeyState(VK_CONTROL) & 0x8000)) {
            std::wstring clip = ReadFromClipboard(hwnd);
            if (!clip.empty()) {
                std::wstring* target = nullptr;
                if (g_state.focusedInput == FocusedInput::LoginIdentifier) target = &g_state.loginIdentifier;
                else if (g_state.focusedInput == FocusedInput::LoginPassword) target = &g_state.loginPassword;
                else if (g_state.focusedInput == FocusedInput::SnowflakeId) target = &g_state.snowflakeInput;
                else if (g_state.focusedInput == FocusedInput::ModalToken) target = &g_state.modalToken;
                if (target) {
                    for (wchar_t c : clip) {
                        if (c >= 32 && c < 127) target->push_back(c);
                    }
                    if (g_state.focusedInput == FocusedInput::SnowflakeId) {
                        g_state.snowflakeDecoded = ApiClient::DecodeSnowflake(*target);
                    }
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        break;
    }

    case WM_CHAR: {
        wchar_t ch = (wchar_t)wParam;
        std::wstring* target = nullptr;
        if (g_state.focusedInput == FocusedInput::LoginIdentifier) target = &g_state.loginIdentifier;
        else if (g_state.focusedInput == FocusedInput::LoginPassword) target = &g_state.loginPassword;
        else if (g_state.focusedInput == FocusedInput::SnowflakeId) target = &g_state.snowflakeInput;
        else if (g_state.focusedInput == FocusedInput::ModalToken) target = &g_state.modalToken;
        if (target) {
            if (ch == L'\b') {
                if (!target->empty()) target->pop_back();
            } else if (ch >= 32 && ch < 127) {
                target->push_back(ch);
            }
            if (g_state.focusedInput == FocusedInput::SnowflakeId) {
                g_state.snowflakeDecoded = ApiClient::DecodeSnowflake(*target);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    }

    case WM_APP_TRAY_NOTIFY: {
        // NOTIFYICON_VERSION_4 packs the notification event into LOWORD(lParam)
        UINT ev = LOWORD(lParam);
        if (ev == WM_LBUTTONUP || ev == WM_LBUTTONDBLCLK ||
            ev == NIN_SELECT || ev == NIN_KEYSELECT || ev == NIN_BALLOONUSERCLICK) {
            RestoreAndActivateWindow(hwnd);
        } else if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
            ShellIntegration::ShowTrayContextMenu(hwnd, g_state);
        }
        return 0;
    }

    case WM_COMMAND: {
        UINT cmd = LOWORD(wParam);
        if (cmd == IDM_TRAY_OPEN) {
            RestoreAndActivateWindow(hwnd);
        } else if (cmd == IDM_TRAY_COMPLETE_ALL) {
            g_state.activeTab = NavTab::Quests;
            ApiClient::StartAllQuests(g_state);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (cmd == IDM_TRAY_STOP_QUEST) {
            ApiClient::CancelRunningQuests(g_state);
            ShellIntegration::UpdateTaskbarProgress(hwnd, g_state);
            ShellIntegration::UpdateTrayIconState(hwnd, g_state);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (cmd == IDM_TRAY_THEME_TOGGLE) {
            g_state.userOverrodeTheme = true;
            g_state.darkMode = !g_state.darkMode;
            g_state.accentColor = ShellIntegration::QuerySystemAccentColor(g_state.darkMode);
            ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
            ApiClient::SaveAuthToDisk(g_state);
            InvalidateRect(hwnd, nullptr, FALSE);
        } else if (cmd == IDM_TRAY_INSTALL_APP) {
            ShellIntegration::RegisterStartMenuAndInstalledApp(hwnd, true);
        } else if (cmd == IDM_TRAY_EXIT) {
            ApiClient::FlushStateIfDirty(g_state);
            ShellIntegration::RemoveTrayIcon(hwnd);
            DestroyWindow(hwnd);
        }
        return 0;
    }

    case WM_CLOSE: {
        if (g_state.benchMode) {
            DestroyWindow(hwnd);
            return 0;
        }
        // Natural background behavior for a Discord Quests utility: hide to notification area with zero idle CPU
        ApiClient::FlushStateIfDirty(g_state);
        ShowWindow(hwnd, SW_HIDE);
        if (!g_shownCloseToTrayTip) {
            g_shownCloseToTrayTip = true;
            ShellIntegration::ShowNativeNotification(
                hwnd,
                L"Discord Helper is running in the background",
                L"Use the system tray icon to check quest progress or exit at any time.");
        }
        return 0;
    }

    case WM_TIMER:
        if (wParam == TIMER_ANIM_PROGRESS) {
            bool keepRunning = false;
            {
                std::lock_guard<std::mutex> lk(g_state.mu);
                float target = static_cast<float>(g_state.activeProgress.percent);
                float diff = target - g_state.animatedProgressPct;
                if (std::fabs(diff) > 0.25f) {
                    g_state.animatedProgressPct += diff * 0.28f;
                    keepRunning = true;
                } else {
                    g_state.animatedProgressPct = target;
                }
            }
            if (!keepRunning && g_animTimerActive) {
                KillTimer(hwnd, TIMER_ANIM_PROGRESS);
                g_animTimerActive = false;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (wParam == TIMER_BENCH_STEP && g_state.benchMode) {
            g_benchPhase++;
            if (g_benchPhase == 1) {
                g_state.isAuthenticated = true;
                g_state.user = {L"101", L"thy", L"okaythi", L"thy@nixlabs.tech", L"", L"PRO", L"en-GB"};
                g_state.sessionCookie = "bench-session";
                ApiClient::SaveAuthToDisk(g_state);
                HandleClick(hwnd, 80, 115); // Account tab
            } else if (g_benchPhase == 2) {
                HandleClick(hwnd, 80, 168); // Quests tab
                ApiClient::FetchQuestsList(g_state, true);
            } else if (g_benchPhase == 3) {
                if (!g_state.quests.empty()) ApiClient::StartSingleQuest(g_state, g_state.quests[0].id);
            } else if (g_benchPhase == 4) {
                ApiClient::CancelRunningQuests(g_state);
                HandleClick(hwnd, 80, 220); // Snowflakes tab
                g_state.snowflakeInput = L"213379314000000000";
                g_state.snowflakeDecoded = ApiClient::DecodeSnowflake(g_state.snowflakeInput);
            } else if (g_benchPhase >= 5) {
                KillTimer(hwnd, TIMER_BENCH_STEP);
                DestroyWindow(hwnd);
            }
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;

    case WM_APP_STATE_UPDATED: {
        ShellIntegration::SyncSystemThemeAndAccent(g_state);
        ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
        ShellIntegration::UpdateTaskbarProgress(hwnd, g_state);
        ShellIntegration::UpdateTrayIconState(hwnd, g_state);

        bool fireNotify = false;
        std::wstring nTitle, nMsg;
        DWORD nFlags = 0x00000004;
        {
            std::lock_guard<std::mutex> lk(g_state.mu);
            if (g_state.hasPendingNotification) {
                fireNotify = true;
                nTitle = g_state.notifyTitle;
                nMsg = g_state.notifyMessage;
                nFlags = g_state.notifyFlags;
                g_state.hasPendingNotification = false;
            }
        }
        if (fireNotify) {
            ShellIntegration::ShowNativeNotification(hwnd, nTitle.c_str(), nMsg.c_str(), nFlags);
        }

        SyncProgressAnimationTimer(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }

    case WM_DESTROY:
        if (g_animTimerActive) {
            KillTimer(hwnd, TIMER_ANIM_PROGRESS);
            g_animTimerActive = false;
        }
        ApiClient::FlushStateIfDirty(g_state);
        ShellIntegration::RemoveTrayIcon(hwnd);
        ShellIntegration::ReleaseTaskbarList();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    ShellIntegration::InitializeProcessIdentity();

    if (lpCmdLine) {
        if (wcsstr(lpCmdLine, L"--uninstall") != nullptr) {
            ShellIntegration::UnregisterStartMenuAndInstalledApp();
            return 0;
        }
        if (wcsstr(lpCmdLine, L"--install") != nullptr) {
            ShellIntegration::RegisterStartMenuAndInstalledApp(nullptr, false);
            return 0;
        }
        if (wcsstr(lpCmdLine, L"--bench-trace") != nullptr) {
            g_state.benchMode = true;
        }
    }

    if (!g_state.benchMode && ShellIntegration::ActivateExistingInstanceIfRunning()) {
        return 0;
    }

    ShellIntegration::SyncSystemThemeAndAccent(g_state);

    int smCxSm = GetSystemMetrics(SM_CXSMICON);
    int smCxLg = GetSystemMetrics(SM_CXICON);
    HICON hClassIconSm = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        smCxSm > 0 ? smCxSm : 16, smCxSm > 0 ? smCxSm : 16, LR_DEFAULTCOLOR));
    HICON hClassIconLg = static_cast<HICON>(LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        smCxLg > 0 ? smCxLg : 32, smCxLg > 0 ? smCxLg : 32, LR_DEFAULTCOLOR));
    if (!hClassIconSm) hClassIconSm = ShellIntegration::CreateBrandIconDIB(16);
    if (!hClassIconLg) hClassIconLg = ShellIntegration::CreateBrandIconDIB(32);

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = hClassIconLg;
    wc.hIconSm = hClassIconSm;
    wc.lpszClassName = L"NixlabsDiscordHelperWnd";
    RegisterClassExW(&wc);

    UINT initDpi = GetDpiForSystem();
    if (initDpi == 0) initDpi = 96;
    int initW = MulDiv(1040, (int)initDpi, 96);
    int initH = MulDiv(720, (int)initDpi, 96);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Discord Helper",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, initW, initH,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    g_state.hwnd = hwnd;
    UINT wndDpi = GetDpiForWindow(hwnd);
    if (wndDpi == 0) wndDpi = initDpi;
    g_state.dpiScale = (float)wndDpi / 96.0f;

    ShellIntegration::UpdateWindowIconsForDpi(hwnd, hInstance, wndDpi);
    ShellIntegration::ApplyWindows11FrameAttributes(hwnd, g_state.darkMode);
    g_renderer.Initialize(hwnd);
    g_renderer.SetDpi(wndDpi);

    // Show and paint the initial Direct2D frame immediately before any disk or network I/O
    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    // Initialize system tray notification icon and ITaskbarList3 progress indicator
    ShellIntegration::InitializeTrayIcon(hwnd, hInstance);
    ShellIntegration::InitializeTaskbarList(hwnd);

    // Load persisted state, validate session, and ensure Start Menu / Installed Apps registration off the UI thread
    bool isBench = g_state.benchMode;
    std::thread([isBench]() {
        ApiClient::LoadStateFromDisk(g_state);
        ApiClient::StartInitialSessionCheck(g_state);
        if (!isBench) {
            ShellIntegration::RegisterStartMenuAndInstalledApp(nullptr, false);
        }
        if (g_state.hwnd) {
            PostMessageW(g_state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
        }
    }).detach();

    if (g_state.benchMode) {
        SetTimer(hwnd, TIMER_BENCH_STEP, 300, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (hClassIconSm) DestroyIcon(hClassIconSm);
    if (hClassIconLg) DestroyIcon(hClassIconLg);
    return 0;
}
