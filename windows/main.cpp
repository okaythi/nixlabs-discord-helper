#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <winhttp.h>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <cstdlib>
#include <ctime>

#define WEBVIEW_MSWEBVIEW2_BUILTIN_IMPL 1
#define WEBVIEW_MSWEBVIEW2_EXPLICIT_LINK 1
#include "webview.h"

#include "app_state.h"
#include "shell_integration.h"

static AppState g_state;
static webview::webview* g_pWebview = nullptr;
static std::atomic<bool> g_running{true};
static int g_backendPort = 0;
static PROCESS_INFORMATION g_backendPi = {0};
static HANDLE g_backendJob = nullptr;

static void RestoreAndActivateWindow(HWND hwnd) {
    if (!hwnd) return;
    if (!IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_SHOW);
    }
    if (IsIconic(hwnd)) {
        ShowWindow(hwnd, SW_RESTORE);
    }
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);
}

static bool StartBackendServer(int port) {
    g_backendPort = port;

    // Create Windows Job Object with KILL_ON_JOB_CLOSE to guarantee no orphaned backend processes
    g_backendJob = CreateJobObjectW(nullptr, nullptr);
    if (g_backendJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {0};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(g_backendJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring dir = exePath;
    size_t lastSlash = dir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) {
        dir = dir.substr(0, lastSlash);
    }

    std::wstring binPath = dir + L"\\nixlabs-discord-helper-bin.exe";
    if (GetFileAttributesW(binPath.c_str()) == INVALID_FILE_ATTRIBUTES) {
        binPath = L"nixlabs-discord-helper-bin.exe";
    }

    std::wstring cmd = L"\"" + binPath + L"\" -port " + std::to_wstring(port);

    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
    cmdBuf.push_back(L'\0');

    BOOL ok = CreateProcessW(
        nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW | CREATE_BREAKAWAY_FROM_JOB,
        nullptr, dir.c_str(), &si, &g_backendPi);

    if (!ok) {
        ok = CreateProcessW(
            nullptr, cmdBuf.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW,
            nullptr, dir.c_str(), &si, &g_backendPi);
    }

    if (ok && g_backendJob) {
        AssignProcessToJobObject(g_backendJob, g_backendPi.hProcess);
    }
    return (ok != FALSE);
}

static void WatchBackendProgress(HWND hwnd, int port) {
    bool lastCompletedState = false;
    std::string lastQuestId;

    while (g_running.load(std::memory_order_relaxed)) {
        HINTERNET hSession = WinHttpOpen(L"NixlabsDiscordHelper/1.0",
                                         WINHTTP_ACCESS_TYPE_NO_PROXY,
                                         WINHTTP_NO_PROXY_NAME,
                                         WINHTTP_NO_PROXY_BYPASS, 0);
        if (hSession) {
            WinHttpSetTimeouts(hSession, 800, 800, 800, 800);
            HINTERNET hConnect = WinHttpConnect(hSession, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
            if (hConnect) {
                HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", L"/api/quests/progress",
                                                       nullptr, WINHTTP_NO_REFERER,
                                                       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
                if (hRequest) {
                    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                        WinHttpReceiveResponse(hRequest, nullptr)) {
                        DWORD dwSize = 0;
                        std::string json;
                        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
                            std::vector<char> buf(dwSize + 1, 0);
                            DWORD dwRead = 0;
                            if (WinHttpReadData(hRequest, buf.data(), dwSize, &dwRead) && dwRead > 0) {
                                json.append(buf.data(), dwRead);
                            }
                        }

                        if (!json.empty()) {
                            auto extractBool = [](const std::string& j, const char* key) {
                                size_t p = j.find(std::string("\"") + key + "\"");
                                if (p == std::string::npos) return false;
                                p = j.find(':', p);
                                if (p == std::string::npos) return false;
                                while (p < j.size() && (j[p] == ':' || j[p] == ' ' || j[p] == '\t')) p++;
                                return j.compare(p, 4, "true") == 0;
                            };
                            auto extractDouble = [](const std::string& j, const char* key) {
                                size_t p = j.find(std::string("\"") + key + "\"");
                                if (p == std::string::npos) return 0.0;
                                p = j.find(':', p);
                                if (p == std::string::npos) return 0.0;
                                return std::atof(j.c_str() + p + 1);
                            };
                            auto extractString = [](const std::string& j, const char* key) {
                                std::string pat = std::string("\"") + key + "\"";
                                size_t p = j.find(pat);
                                if (p == std::string::npos) return std::string();
                                p = j.find('"', p + pat.size() + 1);
                                if (p == std::string::npos) return std::string();
                                size_t end = j.find('"', p + 1);
                                if (end == std::string::npos) return std::string();
                                return j.substr(p + 1, end - p - 1);
                            };

                            bool isRunning = extractBool(json, "running");
                            bool isCompleted = extractBool(json, "completed");
                            double pct = extractDouble(json, "percent");
                            std::string qName = extractString(json, "quest_name");
                            std::string qId = extractString(json, "quest_id");
                            std::string err = extractString(json, "error");

                            bool justCompleted = false;
                            bool justFailed = false;
                            {
                                std::lock_guard<std::mutex> lk(g_state.mu);
                                g_state.activeProgress.running = isRunning;
                                g_state.activeProgress.completed = isCompleted;
                                g_state.activeProgress.percent = pct;
                                if (!qName.empty()) {
                                    g_state.activeProgress.questName = std::wstring(qName.begin(), qName.end());
                                }
                                if (!err.empty()) {
                                    g_state.activeProgress.errorText = std::wstring(err.begin(), err.end());
                                    justFailed = true;
                                }

                                if (isCompleted && !lastCompletedState) {
                                    justCompleted = true;
                                }
                                lastCompletedState = isCompleted;
                                lastQuestId = qId;
                            }

                            ShellIntegration::UpdateTaskbarProgress(hwnd, g_state);
                            ShellIntegration::UpdateTrayIconState(hwnd, g_state);

                            if (justCompleted && !qName.empty()) {
                                std::wstring wName(qName.begin(), qName.end());
                                ShellIntegration::ShowNativeNotification(
                                    hwnd, L"Quest Completed",
                                    (wName + L" completed successfully!").c_str(),
                                    0x00000004 /* NIIF_USER */);
                            } else if (justFailed && !err.empty()) {
                                std::wstring wErr(err.begin(), err.end());
                                ShellIntegration::ShowNativeNotification(
                                    hwnd, L"Quest Completion Failed",
                                    wErr.c_str(),
                                    0x00000003 /* NIIF_ERROR */);
                            }
                        }
                    }
                    WinHttpCloseHandle(hRequest);
                }
                WinHttpCloseHandle(hConnect);
            }
            WinHttpCloseHandle(hSession);
        }

        bool isActivelyRunning = false;
        {
            std::lock_guard<std::mutex> lk(g_state.mu);
            isActivelyRunning = g_state.activeProgress.running;
        }
        for (int i = 0; i < (isActivelyRunning ? 15 : 30); ++i) {
            if (!g_running.load(std::memory_order_relaxed)) break;
            Sleep(100);
        }
    }
}

static LRESULT CALLBACK WebviewWindowSubclass(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam,
                                              UINT_PTR uIdSubclass, DWORD_PTR /*dwRefData*/) {
    switch (uMsg) {
    case WM_APP_TRAY_NOTIFY:
        if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) {
            ShellIntegration::ShowTrayContextMenu(hwnd, g_state);
            return 0;
        } else if (lParam == WM_LBUTTONDBLCLK || lParam == NIN_SELECT) {
            RestoreAndActivateWindow(hwnd);
            return 0;
        }
        break;

    case WM_COMMAND:
        switch (LOWORD(wParam)) {
        case IDM_TRAY_OPEN:
            RestoreAndActivateWindow(hwnd);
            return 0;
        case IDM_TRAY_QUEST_STATUS:
            RestoreAndActivateWindow(hwnd);
            return 0;
        case IDM_TRAY_COMPLETE_ALL:
            std::thread([port = g_backendPort]() {
                HINTERNET hS = WinHttpOpen(L"NixlabsDiscordHelper/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
                if (hS) {
                    HINTERNET hC = WinHttpConnect(hS, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
                    if (hC) {
                        HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/quests/complete-all", 0, 0, 0, 0);
                        if (hR) {
                            WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                            WinHttpReceiveResponse(hR, 0);
                            WinHttpCloseHandle(hR);
                        }
                        WinHttpCloseHandle(hC);
                    }
                    WinHttpCloseHandle(hS);
                }
            }).detach();
            return 0;
        case IDM_TRAY_STOP_QUEST:
            std::thread([port = g_backendPort]() {
                HINTERNET hS = WinHttpOpen(L"NixlabsDiscordHelper/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY, 0, 0, 0);
                if (hS) {
                    HINTERNET hC = WinHttpConnect(hS, L"127.0.0.1", static_cast<INTERNET_PORT>(port), 0);
                    if (hC) {
                        HINTERNET hR = WinHttpOpenRequest(hC, L"POST", L"/api/quests/cancel", 0, 0, 0, 0);
                        if (hR) {
                            WinHttpSendRequest(hR, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
                            WinHttpReceiveResponse(hR, 0);
                            WinHttpCloseHandle(hR);
                        }
                        WinHttpCloseHandle(hC);
                    }
                    WinHttpCloseHandle(hS);
                }
            }).detach();
            return 0;
        case IDM_TRAY_THEME_TOGGLE:
            if (g_pWebview) {
                g_pWebview->eval("document.documentElement.classList.toggle('dark');");
            }
            return 0;
        case IDM_TRAY_INSTALL_APP:
            ShellIntegration::RegisterStartMenuAndInstalledApp(hwnd, true);
            return 0;
        case IDM_TRAY_EXIT:
            ShellIntegration::RemoveTrayIcon(hwnd);
            ShellIntegration::ReleaseTaskbarList();
            g_running.store(false);
            if (g_pWebview) g_pWebview->terminate();
            PostQuitMessage(0);
            return 0;
        }
        break;

    case WM_CLOSE:
        ShellIntegration::RemoveTrayIcon(hwnd);
        ShellIntegration::ReleaseTaskbarList();
        g_running.store(false);
        if (g_pWebview) g_pWebview->terminate();
        break;

    case WM_DESTROY:
        RemoveWindowSubclass(hwnd, WebviewWindowSubclass, uIdSubclass);
        break;
    }
    return DefSubclassProc(hwnd, uMsg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR lpCmdLine, int /*nCmdShow*/) {
    srand(static_cast<unsigned int>(time(nullptr)));

    if (lpCmdLine) {
        if (wcsstr(lpCmdLine, L"--uninstall") != nullptr) {
            ShellIntegration::UnregisterStartMenuAndInstalledApp();
            return 0;
        }
        if (wcsstr(lpCmdLine, L"--install") != nullptr) {
            ShellIntegration::RegisterStartMenuAndInstalledApp(nullptr, false);
            return 0;
        }
    }

    if (ShellIntegration::ActivateExistingInstanceIfRunning()) {
        return 0;
    }

    ShellIntegration::InitializeProcessIdentity();
    ShellIntegration::SyncSystemThemeAndAccent(g_state);

    int port = 40000 + (rand() % 20000);
    if (!StartBackendServer(port)) {
        MessageBoxW(nullptr, L"Failed to launch Nixlabs Discord Helper backend server.",
                    L"Startup Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    webview::webview w(false, nullptr);
    g_pWebview = &w;

    HWND hwnd = static_cast<HWND>(w.window());
    if (!hwnd) {
        if (g_backendJob) CloseHandle(g_backendJob);
        return 1;
    }

    g_state.hwnd = hwnd;
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    g_state.dpiScale = static_cast<float>(dpi) / 96.0f;

    ShellIntegration::ApplyWindows11FrameAttributes(hwnd, true);
    ShellIntegration::UpdateWindowIconsForDpi(hwnd, hInstance, dpi);
    ShellIntegration::InitializeTrayIcon(hwnd, hInstance);
    ShellIntegration::InitializeTaskbarList(hwnd);
    ShellIntegration::RegisterStartMenuAndInstalledApp(nullptr, false);

    SetWindowSubclass(hwnd, WebviewWindowSubclass, 1, 0);

    w.set_title("Discord Helper");
    w.set_size(1040, 720, WEBVIEW_HINT_NONE);

    w.init(R"raw(
        (function() {
            function handleExternal(url) {
                if (url && (url.startsWith('http://') || url.startsWith('https://')) && !url.startsWith(location.origin)) {
                    if (window.openExternal_native) {
                        window.openExternal_native(url);
                        return true;
                    }
                }
                return false;
            }
            document.addEventListener('click', function(e) {
                var a = e.target.closest('a');
                if (a && a.href && handleExternal(a.href)) {
                    e.preventDefault();
                    e.stopPropagation();
                }
            }, true);
            var origOpen = window.open;
            window.open = function(url) {
                if (handleExternal(url)) {
                    return null;
                }
                return origOpen.apply(this, arguments);
            };
        })();
    )raw");

    w.bind("openExternal_native", [](std::string req) -> std::string {
        std::string url;
        size_t first = req.find('"');
        if (first != std::string::npos) {
            size_t second = req.find('"', first + 1);
            if (second != std::string::npos) {
                url = req.substr(first + 1, second - first - 1);
            }
        }
        if (!url.empty()) {
            std::wstring wurl(url.begin(), url.end());
            ShellExecuteW(nullptr, L"open", wurl.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        }
        return "{}";
    });

    std::thread watcher([hwnd, port]() {
        WatchBackendProgress(hwnd, port);
    });
    watcher.detach();

    std::string appUrl = "http://127.0.0.1:" + std::to_string(port);
    w.navigate(appUrl);

    w.run();

    g_running.store(false);
    ShellIntegration::RemoveTrayIcon(hwnd);
    ShellIntegration::ReleaseTaskbarList();

    if (g_backendPi.hProcess) {
        TerminateProcess(g_backendPi.hProcess, 0);
        CloseHandle(g_backendPi.hProcess);
        CloseHandle(g_backendPi.hThread);
    }
    if (g_backendJob) {
        CloseHandle(g_backendJob);
    }

    CoUninitialize();
    return 0;
}
