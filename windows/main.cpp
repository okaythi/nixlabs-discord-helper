#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <cstdlib>
#include <ctime>

#define WEBVIEW_MSWEBVIEW2_BUILTIN_IMPL 1
#define WEBVIEW_MSWEBVIEW2_EXPLICIT_LINK 1
#include "webview.h"

#include "shell_integration.h"

static PROCESS_INFORMATION g_backendPi = {0};
static HANDLE g_backendJob = nullptr;

static bool StartBackendServer(int port) {
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

    // Single instance check: bring existing window to front if already open
    static HANDLE s_hSingleInstanceMutex = CreateMutexW(nullptr, FALSE, L"Local\\NixlabsDiscordHelper_SingleInstance_Mutex");
    if (s_hSingleInstanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND hExisting = FindWindowW(L"webview", L"Discord Helper");
        if (!hExisting) hExisting = FindWindowW(nullptr, L"Discord Helper");
        if (hExisting) {
            if (!IsWindowVisible(hExisting)) ShowWindow(hExisting, SW_SHOW);
            if (IsIconic(hExisting)) ShowWindow(hExisting, SW_RESTORE);
            SetForegroundWindow(hExisting);
        }
        return 0;
    }

    ShellIntegration::InitializeProcessIdentity();

    int port = 40000 + (rand() % 20000);
    if (!StartBackendServer(port)) {
        MessageBoxW(nullptr, L"Failed to launch Nixlabs Discord Helper backend server.",
                    L"Startup Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // Brief moment for backend to bind port
    Sleep(250);

    {
        webview::webview w(false, nullptr);

        HWND hwnd = static_cast<HWND>(w.window());
        if (!hwnd || !w.is_valid()) {
            if (g_backendJob) CloseHandle(g_backendJob);
            return 1;
        }

        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0) dpi = 96;

        ShellIntegration::ApplyWindows11FrameAttributes(hwnd, true);
        ShellIntegration::UpdateWindowIconsForDpi(hwnd, hInstance, dpi);
        ShellIntegration::RegisterStartMenuAndInstalledApp(nullptr, false);

        w.set_title("Discord Helper");
        w.set_size(1024, 720, WEBVIEW_HINT_NONE);

        // Intercept external links and open in default web browser (identical to Linux openExternal)
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

        std::string appUrl = "http://127.0.0.1:" + std::to_string(port);
        w.navigate(appUrl);

        w.run();
    }

    if (g_backendPi.hProcess) {
        TerminateProcess(g_backendPi.hProcess, 0);
        CloseHandle(g_backendPi.hProcess);
        CloseHandle(g_backendPi.hThread);
    }
    if (g_backendJob) {
        CloseHandle(g_backendJob);
    }
    if (s_hSingleInstanceMutex) {
        CloseHandle(s_hSingleInstanceMutex);
    }

    return 0;
}
