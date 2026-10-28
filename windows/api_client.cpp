#include "api_client.h"
#include <winhttp.h>
#include <shlobj.h>
#include <shellapi.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <ctime>

#define WM_APP_STATE_UPDATED (WM_USER + 101)

namespace ApiClient {

std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return L"";
    int sz = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    if (sz <= 0) return L"";
    std::wstring res(sz, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &res[0], sz);
    return res;
}

std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int sz = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    if (sz <= 0) return "";
    std::string res(sz, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &res[0], sz, nullptr, nullptr);
    return res;
}

std::string ExtractJsonString(const std::string& json, const char* key) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return "";
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return "";
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    if (pos >= json.size() || json[pos] != '"') return "";
    pos++;
    std::string out;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            pos++;
            if (json[pos] == 'n') out.push_back('\n');
            else if (json[pos] == 't') out.push_back('\t');
            else out.push_back(json[pos]);
        } else {
            out.push_back(json[pos]);
        }
        pos++;
    }
    return out;
}

int ExtractJsonInt(const std::string& json, const char* key, int defaultVal = 0) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    if (pos >= json.size()) return defaultVal;
    return std::atoi(json.c_str() + pos);
}

bool ExtractJsonBool(const std::string& json, const char* key, bool defaultVal = false) {
    std::string pattern = std::string("\"") + key + "\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return defaultVal;
    pos = json.find(':', pos + pattern.size());
    if (pos == std::string::npos) return defaultVal;
    pos++;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t' || json[pos] == '\n' || json[pos] == '\r')) pos++;
    if (pos + 4 <= json.size() && json.compare(pos, 4, "true") == 0) return true;
    if (pos + 5 <= json.size() && json.compare(pos, 5, "false") == 0) return false;
    return defaultVal;
}

// Pass 2 Optimization: Cache config directory path & creation once per process lifetime
const std::wstring& GetConfigDirPath() {
    static std::once_flag s_once;
    static std::wstring s_dir;
    std::call_once(s_once, []() {
        wchar_t xdg[MAX_PATH] = {0};
        DWORD len = GetEnvironmentVariableW(L"XDG_CONFIG_HOME", xdg, MAX_PATH);
        std::wstring baseDir;
        if (len > 0 && len < MAX_PATH) {
            baseDir = xdg;
        } else {
            wchar_t appData[MAX_PATH] = {0};
            if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
                baseDir = appData;
            } else {
                baseDir = L".";
            }
        }
        s_dir = baseDir + L"\\nixlabs-discord-helper";
        CreateDirectoryW(s_dir.c_str(), nullptr);
    });
    return s_dir;
}

// Pass 2 Optimization: Single sequential read using FILE_FLAG_SEQUENTIAL_SCAN and GetFileSizeEx (1 ReadFile syscall instead of 512-byte loop)
std::string ReadSequentialFile(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return "";
    LARGE_INTEGER fileSize = {0};
    if (!GetFileSizeEx(hFile, &fileSize) || fileSize.QuadPart <= 0 || fileSize.QuadPart > (4 * 1024 * 1024)) {
        CloseHandle(hFile);
        return "";
    }
    std::string content(static_cast<size_t>(fileSize.QuadPart), '\0');
    DWORD bytesRead = 0;
    ReadFile(hFile, &content[0], static_cast<DWORD>(content.size()), &bytesRead, nullptr);
    content.resize(bytesRead);
    CloseHandle(hFile);
    return content;
}

// Pass 2 Optimization: Single buffered sequential write without blocking UI thread on FlushFileBuffers barriers
void WriteSequentialFile(const std::wstring& path, const std::string& data) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hFile, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(hFile);
}

void LoadStateFromDisk(AppState& state) {
    std::string stateData = ReadSequentialFile(GetConfigDirPath() + L"\\state.json");
    std::lock_guard<std::mutex> lk(state.mu);
    if (!stateData.empty()) {
        std::string th = ExtractJsonString(stateData, "theme");
        if (!th.empty()) state.darkMode = (th != "light");

        std::string loc = ExtractJsonString(stateData, "locale");
        if (!loc.empty()) state.locale = ResolveLocaleFromCode(Utf8ToWide(loc));

        state.sessionCookie = ExtractJsonString(stateData, "session_cookie");
        state.authUpdatedAt = ExtractJsonString(stateData, "updated_at");
        state.user.name = Utf8ToWide(ExtractJsonString(stateData, "name"));
        state.user.handle = Utf8ToWide(ExtractJsonString(stateData, "handle"));
        state.user.email = Utf8ToWide(ExtractJsonString(stateData, "email"));
        state.user.role = Utf8ToWide(ExtractJsonString(stateData, "role"));
        state.user.language = Utf8ToWide(ExtractJsonString(stateData, "language"));
        if (!state.sessionCookie.empty() && !state.user.handle.empty()) {
            state.isAuthenticated = true;
            state.isLoadingAuth = false;
        }
    }
    if (state.quests.empty()) {
        PopulateSampleQuestsIfOffline(state);
    }
}

void SaveAuthToDisk(AppState& state) {
    state.stateDirty.store(true, std::memory_order_release);
}

void FlushStateIfDirty(AppState& state) {
    if (!state.stateDirty.exchange(false, std::memory_order_acq_rel)) return;
    std::ostringstream oss;
    oss << "{\"theme\":\"" << (state.darkMode ? "dark" : "light")
        << "\",\"locale\":\"" << WideToUtf8(GetLocaleCode(state.locale))
        << "\",\"session_cookie\":\"" << state.sessionCookie
        << "\",\"updated_at\":\"" << state.authUpdatedAt
        << "\",\"user\":{\"name\":\"" << WideToUtf8(state.user.name)
        << "\",\"handle\":\"" << WideToUtf8(state.user.handle)
        << "\",\"email\":\"" << WideToUtf8(state.user.email)
        << "\",\"role\":\"" << WideToUtf8(state.user.role)
        << "\",\"language\":\"" << WideToUtf8(state.user.language)
        << "\"},\"quests\":[";
    for (size_t i = 0; i < state.quests.size(); i++) {
        const auto& q = state.quests[i];
        if (i > 0) oss << ",";
        oss << "{\"id\":\"" << q.id
            << "\",\"done\":" << (int)q.secondsDone
            << ",\"completed\":" << (q.completed ? "true" : "false") << "}";
    }
    oss << "]}";
    WriteSequentialFile(GetConfigDirPath() + L"\\state.json", oss.str());
}

std::string HttpRequestSync(const wchar_t* host, const wchar_t* path, const wchar_t* method,
                            const std::string& body, const std::wstring& extraHeaders, int* outStatus = nullptr) {
    if (outStatus) *outStatus = 0;
    HINTERNET hSession = WinHttpOpen(L"NixlabsDiscordHelper/1.0.14 (Windows NT 10.0; Win64; x64)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    WinHttpSetTimeouts(hSession, 2500, 2500, 3000, 3000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return "";
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, method, path, nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return "";
    }

    if (!extraHeaders.empty()) {
        WinHttpAddRequestHeaders(hRequest, extraHeaders.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL bResults = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                       body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                                       (DWORD)body.size(), (DWORD)body.size(), 0);
    std::string response;
    if (bResults && WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD statusCode = 0;
        DWORD dwSize = sizeof(statusCode);
        WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &dwSize, WINHTTP_NO_HEADER_INDEX);
        if (outStatus) *outStatus = (int)statusCode;

        DWORD dwAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &dwAvailable) && dwAvailable > 0) {
            std::string chunk(dwAvailable, '\0');
            DWORD dwDownloaded = 0;
            if (WinHttpReadData(hRequest, &chunk[0], dwAvailable, &dwDownloaded) && dwDownloaded > 0) {
                chunk.resize(dwDownloaded);
                response.append(chunk);
            } else {
                break;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return response;
}

void StartInitialSessionCheck(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mu);
    state.isLoadingAuth = false;
    if (state.sessionCookie.empty()) {
        state.isAuthenticated = false;
    }
}

void PerformLogin(AppState& state, const std::wstring& identifier, const std::wstring& password) {
    {
        std::lock_guard<std::mutex> lk(state.mu);
        state.isSubmittingAuth = true;
        state.authError.clear();
    }
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);

    std::thread([&state, identifier, password]() {
        std::string payload = "{\"identifier\":\"" + WideToUtf8(identifier) + "\",\"password\":\"" + WideToUtf8(password) + "\"}";
        int status = 0;
        std::string resp = HttpRequestSync(L"accounts.nixlabs.tech", L"/api/login", L"POST",
                                           payload, L"Content-Type: application/json\r\nAccept: application/json\r\n", &status);
        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.isSubmittingAuth = false;
            if (status == 200) {
                state.isAuthenticated = true;
                state.user.name = identifier;
                state.user.handle = identifier;
                state.user.role = L"MEMBER";
                state.user.language = L"en-GB";
                SaveAuthToDisk(state);
            } else {
                std::string err = ExtractJsonString(resp, "error");
                state.authError = err.empty() ? L"Authentication failed." : Utf8ToWide(err);
            }
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void PerformLogout(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mu);
    state.isAuthenticated = false;
    state.sessionCookie.clear();
    state.user = SessionUser{};
    state.discordUser = DiscordUser{};
    state.stateDirty.store(true, std::memory_order_release);
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);
}

void OpenCreateAccountInBrowser(AppState& state) {
    {
        std::lock_guard<std::mutex> lk(state.mu);
        state.registerUrl = L"https://accounts.nixlabs.tech/login?mode=signup#signup";
        state.loginMode = LoginMode::AwaitingBrowser;
    }
    ShellExecuteW(nullptr, L"open", L"https://accounts.nixlabs.tech/login?mode=signup#signup", nullptr, nullptr, SW_SHOWNORMAL);
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);
}

} // namespace ApiClient
