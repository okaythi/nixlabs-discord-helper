#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <cstdio>
#include <cwchar>
#include "app_state.h"
#include "api_client.h"
#include "ui_renderer.h"

#define WM_APP_STATE_UPDATED (WM_USER + 101)
#define TIMER_BENCH_STEP 1

static AppState g_state;
static UiRenderer g_renderer;
static ULONG_PTR g_gdiplusToken = 0;
static int g_benchPhase = 0;

static void ApplyWindows11Theme(HWND hwnd, bool dark) {
    BOOL useDark = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
    int mica = 2; /* DWMSBT_MAINWINDOW */
    DwmSetWindowAttribute(hwnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &mica, sizeof(mica));
}

// Baseline icon loading: mirrors launcher/nixlabs-discord-helper.js lines 75-99
// Loops over 8 icon sizes on the UI thread, probing and decoding each loose PNG file via GDI+
static void LoadLooseIconsSyncBaseline(HWND hwnd) {
    const int iconSizes[] = {16, 24, 32, 48, 64, 128, 256, 512};
    HICON hAssignedSmall = nullptr;
    HICON hAssignedBig = nullptr;
    for (int s : iconSizes) {
        wchar_t relPath[MAX_PATH];
        swprintf(relPath, MAX_PATH, L"assets\\icons\\%dx%d\\nixlabs-discord-helper.png", s, s);
        DWORD attr = GetFileAttributesW(relPath);
        if (attr != INVALID_FILE_ATTRIBUTES) {
            Gdiplus::Bitmap* bmp = Gdiplus::Bitmap::FromFile(relPath);
            if (bmp && bmp->GetLastStatus() == Gdiplus::Ok) {
                HICON hIcon = nullptr;
                if (bmp->GetHICON(&hIcon) == Gdiplus::Ok && hIcon) {
                    if (s == 16 && !hAssignedSmall) {
                        hAssignedSmall = hIcon;
                        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
                    } else if (s == 256 && !hAssignedBig) {
                        hAssignedBig = hIcon;
                        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
                    } else {
                        DestroyIcon(hIcon);
                    }
                }
                delete bmp;
            }
        }
    }
}

// Baseline child backend process spawn: mirrors launcher/nixlabs-discord-helper.js lines 34-58
static void SpawnBackendChildProcessBaseline(AppState& state) {
    const wchar_t* candidates[] = {
        L"nixlabs-discord-helper-bin.exe",
        L"backend\\nixlabs-discord-helper-bin.exe"
    };
    for (const wchar_t* bin : candidates) {
        if (GetFileAttributesW(bin) != INVALID_FILE_ATTRIBUTES) {
            STARTUPINFOW si = {sizeof(si)};
            si.dwFlags = STARTF_USESHOWWINDOW;
            si.wShowWindow = SW_HIDE;
            PROCESS_INFORMATION pi = {0};
            wchar_t cmdLine[MAX_PATH];
            swprintf(cmdLine, MAX_PATH, L"\"%s\" -port 45731", bin);
            if (CreateProcessW(nullptr, cmdLine, nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                state.childBackendProcess = pi.hProcess;
                CloseHandle(pi.hThread);
                break;
            }
        }
    }
}

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

static void HandleClick(HWND hwnd, int x, int y) {
    int hitId = HIT_NONE;
    {
        std::lock_guard<std::mutex> lk(g_state.mu);
        for (auto it = g_state.clickTargets.rbegin(); it != g_state.clickTargets.rend(); ++it) {
            if (x >= it->rc.left && x <= it->rc.right && y >= it->rc.top && y <= it->rc.bottom) {
                hitId = it->id;
                break;
            }
        }
    }
    if (hitId == HIT_NONE) return;

    if (hitId == HIT_THEME_TOGGLE) {
        g_state.darkMode = !g_state.darkMode;
        ApplyWindows11Theme(hwnd, g_state.darkMode);
        ApiClient::SaveAuthToDisk(g_state);
    } else if (hitId == HIT_LOCALE_CYCLE) {
        g_state.locale = static_cast<LocaleId>(((int)g_state.locale + 1) % 6);
        ApiClient::SaveAuthToDisk(g_state);
    } else if (hitId == HIT_LOGOUT) {
        ApiClient::PerformLogout(g_state);
    } else if (hitId == HIT_TAB_ACCOUNT) {
        g_state.activeTab = NavTab::Account;
        g_state.scrollY = 0;
        // Baseline redundant reload on tab switch
        ApiClient::LoadStateFromDisk(g_state);
        LoadLooseIconsSyncBaseline(hwnd);
    } else if (hitId == HIT_TAB_QUESTS) {
        g_state.activeTab = NavTab::Quests;
        g_state.scrollY = 0;
        ApiClient::LoadStateFromDisk(g_state);
        ApiClient::FetchQuestsList(g_state, false);
    } else if (hitId == HIT_TAB_SNOWFLAKES) {
        g_state.activeTab = NavTab::Snowflakes;
        g_state.scrollY = 0;
        ApiClient::LoadStateFromDisk(g_state);
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
        g_renderer.Resize(LOWORD(lParam), HIWORD(lParam));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = reinterpret_cast<MINMAXINFO*>(lParam);
        mmi->ptMinTrackSize.x = 480;
        mmi->ptMinTrackSize.y = 400;
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        g_renderer.Render(g_state);
        EndPaint(hwnd, &ps);
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
    case WM_TIMER:
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
    case WM_APP_STATE_UPDATED:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_DESTROY:
        if (g_state.childBackendProcess) {
            TerminateProcess(g_state.childBackendProcess, 0);
            CloseHandle(g_state.childBackendProcess);
            g_state.childBackendProcess = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int nCmdShow) {
    if (lpCmdLine && wcsstr(lpCmdLine, L"--bench-trace") != nullptr) {
        g_state.benchMode = true;
    }

    // Baseline: initialize GDI+ and synchronously read config, scrape build number, and spawn backend before showing window
    Gdiplus::GdiplusStartupInput gdiplusStartupInput;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusStartupInput, nullptr);

    ApiClient::LoadStateFromDisk(g_state);
    ApiClient::ScrapeDiscordBuildNumberSync(g_state);
    SpawnBackendChildProcessBaseline(g_state);
    Sleep(300); // Baseline: 300ms startup wait from launcher/nixlabs-discord-helper.js:213

    WNDCLASSEXW wc = {sizeof(WNDCLASSEXW)};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"NixlabsDiscordHelperWnd";
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, wc.lpszClassName, L"Discord Helper",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 720,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) return 1;

    g_state.hwnd = hwnd;
    ApplyWindows11Theme(hwnd, g_state.darkMode);
    LoadLooseIconsSyncBaseline(hwnd);
    g_renderer.Initialize(hwnd);
    ApiClient::StartInitialSessionCheck(g_state);

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    if (g_state.benchMode) {
        SetTimer(hwnd, TIMER_BENCH_STEP, 300, nullptr);
    }

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_gdiplusToken) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
    }
    return 0;
}
