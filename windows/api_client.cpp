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

// Pass 2 Optimization: Single sequential read using FILE_FLAG_SEQUENTIAL_SCAN and GetFileSizeEx
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

// Pass 2 Optimization: Single buffered sequential write without blocking UI thread
void WriteSequentialFile(const std::wstring& path, const std::string& data) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hFile, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(hFile);
}

static void ApplyUserJsonToStateLocked(AppState& state, const std::string& json) {
    std::string name = ExtractJsonString(json, "name");
    std::string handle = ExtractJsonString(json, "handle");
    if (handle.empty()) handle = ExtractJsonString(json, "username");
    std::string email = ExtractJsonString(json, "email");
    std::string role = ExtractJsonString(json, "role");
    if (role.empty()) role = ExtractJsonString(json, "tier");
    std::string lang = ExtractJsonString(json, "language");
    if (lang.empty()) lang = ExtractJsonString(json, "locale");

    if (!name.empty()) state.user.name = Utf8ToWide(name);
    if (!handle.empty()) state.user.handle = Utf8ToWide(handle);
    if (!email.empty()) state.user.email = Utf8ToWide(email);
    if (!role.empty()) state.user.role = Utf8ToWide(role);
    if (!lang.empty()) {
        state.user.language = Utf8ToWide(lang);
        // Automatically resolve UI language from the Nixlabs account language (matching Linux behavior)
        state.locale = ResolveLocaleFromCode(state.user.language);
    }
    std::string avatar = ExtractJsonString(json, "avatar");
    if (avatar.empty()) avatar = ExtractJsonString(json, "avatar_url");
    if (!avatar.empty()) state.user.avatarUrl = Utf8ToWide(avatar);
    state.account.accountStanding = ExtractJsonInt(json, "account_standing", 0);
    state.account.banned = ExtractJsonBool(json, "banned", false);
}

void LoadStateFromDisk(AppState& state) {
    std::string stateData = ReadSequentialFile(GetConfigDirPath() + L"\\state.json");
    // Also read auth.json if present (shared with backend/internal/auth/auth.go)
    std::string authData = ReadSequentialFile(GetConfigDirPath() + L"\\auth.json");
    if (authData.empty()) {
        wchar_t userProfile[MAX_PATH] = {0};
        if (GetEnvironmentVariableW(L"USERPROFILE", userProfile, MAX_PATH) > 0) {
            authData = ReadSequentialFile(std::wstring(userProfile) + L"\\.config\\nixlabs-discord-helper\\auth.json");
        }
    }
    std::lock_guard<std::mutex> lk(state.mu);
    if (!stateData.empty()) {
        state.userOverrodeTheme = ExtractJsonBool(stateData, "user_theme_override", false);
        std::string th = ExtractJsonString(stateData, "theme");
        if (!th.empty() && state.userOverrodeTheme) {
            state.darkMode = (th != "light");
        }

        state.sessionCookie = ExtractJsonString(stateData, "session_cookie");
        state.authUpdatedAt = ExtractJsonString(stateData, "updated_at");
        ApplyUserJsonToStateLocked(state, stateData);

        // Restore cached Discord profile if saved
        std::string dId = ExtractJsonString(stateData, "discord_id");
        std::string dUser = ExtractJsonString(stateData, "discord_username");
        if (!dId.empty() && !dUser.empty()) {
            state.discordUser.connected = true;
            state.discordUser.id = Utf8ToWide(dId);
            state.discordUser.username = Utf8ToWide(dUser);
            state.discordUser.globalName = Utf8ToWide(ExtractJsonString(stateData, "discord_global_name"));
            state.discordUser.bannerColor = Utf8ToWide(ExtractJsonString(stateData, "discord_banner_color"));
            state.discordUser.createdAt = Utf8ToWide(ExtractJsonString(stateData, "discord_created_at"));
            state.discordUser.avatarUrl = Utf8ToWide(ExtractJsonString(stateData, "discord_avatar_url"));
            state.discordUser.bannerUrl = Utf8ToWide(ExtractJsonString(stateData, "discord_banner_url"));
        }
    }
    if ((state.sessionCookie.empty() || state.sessionCookie == "bench-session") && !authData.empty()) {
        std::string authCookie = ExtractJsonString(authData, "session_cookie");
        if (!authCookie.empty() && authCookie != "bench-session") {
            state.sessionCookie = authCookie;
            state.authUpdatedAt = ExtractJsonString(authData, "updated_at");
            ApplyUserJsonToStateLocked(state, authData);
        } else if (state.user.handle.empty()) {
            ApplyUserJsonToStateLocked(state, authData);
        }
    }
    if (!state.sessionCookie.empty() && state.sessionCookie != "bench-session" && !state.user.handle.empty()) {
        state.isAuthenticated = true;
        state.isLoadingAuth = false;
    } else if (state.sessionCookie == "bench-session") {
        // Clear synthetic benchmark session so real interactive runs authenticate with a real Nixlabs session cookie
        state.sessionCookie.clear();
        state.isAuthenticated = false;
        state.isLoadingAuth = false;
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
    static const char* const kLocaleCodes[] = {"en-uk", "en-us", "fr-be", "ja", "pt-br", "pt-pt"};
    int locIdx = static_cast<int>(state.locale);
    if (locIdx < 0 || locIdx > 5) locIdx = 0;
    std::ostringstream oss;
    oss << "{\"theme\":\"" << (state.darkMode ? "dark" : "light")
        << "\",\"user_theme_override\":" << (state.userOverrodeTheme ? "true" : "false")
        << ",\"locale\":\"" << kLocaleCodes[locIdx]
        << "\",\"session_cookie\":\"" << state.sessionCookie
        << "\",\"updated_at\":\"" << state.authUpdatedAt
        << "\",\"user\":{\"name\":\"" << WideToUtf8(state.user.name)
        << "\",\"handle\":\"" << WideToUtf8(state.user.handle)
        << "\",\"email\":\"" << WideToUtf8(state.user.email)
        << "\",\"role\":\"" << WideToUtf8(state.user.role)
        << "\",\"language\":\"" << WideToUtf8(state.user.language)
        << "\",\"avatar_url\":\"" << WideToUtf8(state.user.avatarUrl)
        << "\"},\"discord_id\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.id) : "")
        << "\",\"discord_username\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.username) : "")
        << "\",\"discord_global_name\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.globalName) : "")
        << "\",\"discord_banner_color\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.bannerColor) : "")
        << "\",\"discord_created_at\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.createdAt) : "")
        << "\",\"discord_avatar_url\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.avatarUrl) : "")
        << "\",\"discord_banner_url\":\"" << (state.discordUser.connected ? WideToUtf8(state.discordUser.bannerUrl) : "")
        << "\",\"quests\":[";
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
                            const std::string& body, const std::wstring& extraHeaders, int* outStatus = nullptr,
                            std::string* outSessionCookie = nullptr) {
    if (outStatus) *outStatus = 0;
    HINTERNET hSession = WinHttpOpen(L"NixlabsDiscordHelper/1.0.14 (Windows NT 10.0; Win64; x64)",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return "";
    WinHttpSetTimeouts(hSession, 3500, 3500, 5000, 5000);

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

        // Extract _nixlabs_session cookie from Set-Cookie headers if requested
        if (outSessionCookie) {
            DWORD index = 0;
            while (true) {
                DWORD cbCookie = 0;
                WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                    WINHTTP_NO_OUTPUT_BUFFER, &cbCookie, &index);
                if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || cbCookie == 0) break;
                std::wstring cookieHeader(cbCookie / sizeof(wchar_t), L'\0');
                if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_SET_COOKIE, WINHTTP_HEADER_NAME_BY_INDEX,
                                        &cookieHeader[0], &cbCookie, &index)) {
                    std::string cookieUtf8 = WideToUtf8(cookieHeader);
                    size_t pos = cookieUtf8.find("_nixlabs_session=");
                    if (pos != std::string::npos) {
                        std::string val = cookieUtf8.substr(pos + 17);
                        size_t semi = val.find(';');
                        if (semi != std::string::npos) val = val.substr(0, semi);
                        while (!val.empty() && (val.back() == '\0' || val.back() == '\r' || val.back() == '\n')) {
                            val.pop_back();
                        }
                        if (!val.empty()) {
                            *outSessionCookie = val;
                            break;
                        }
                    }
                } else {
                    break;
                }
            }
        }

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
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        cookie = state.sessionCookie;
        if (cookie.empty()) {
            state.isLoadingAuth = false;
            state.isAuthenticated = false;
            return;
        }
    }

    // Verify / refresh Nixlabs profile against accounts.nixlabs.tech/api/profile (exactly like auth.go GetSession)
    if (cookie != "bench-session") {
        std::wstring headers = L"Cookie: _nixlabs_session=" + Utf8ToWide(cookie) +
                               L"\r\nAuthorization: Bearer " + Utf8ToWide(cookie) +
                               L"\r\nAccept: application/json\r\n";
        int status = 0;
        std::string profileResp = HttpRequestSync(L"accounts.nixlabs.tech", L"/api/profile", L"GET", "", headers, &status);
        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.isLoadingAuth = false;
            if (status == 200 && !profileResp.empty()) {
                ApplyUserJsonToStateLocked(state, profileResp);
                state.isAuthenticated = !state.account.banned;
                SaveAuthToDisk(state);
            } else if (status == 401) {
                state.isAuthenticated = false;
                state.sessionCookie.clear();
                SaveAuthToDisk(state);
                return;
            }
        }
        // Automatically fetch the connected Discord account & token from Nixlabs Accounts
        FetchDiscordUserProfile(state, false);
    } else {
        std::lock_guard<std::mutex> lk(state.mu);
        state.isLoadingAuth = false;
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
        std::string sessionCookie;
        std::wstring loginHeaders = L"Content-Type: application/json\r\nAccept: application/json\r\n"
                                    L"X-Nixlabs-Client: discord-helper\r\nX-Nixlabs-Client-Version: 1.0.14\r\n";
        std::string resp = HttpRequestSync(L"accounts.nixlabs.tech", L"/api/login", L"POST",
                                           payload, loginHeaders, &status, &sessionCookie);
        if (status == 200) {
            if (sessionCookie.empty()) {
                sessionCookie = ExtractJsonString(resp, "session_cookie");
                if (sessionCookie.empty()) sessionCookie = ExtractJsonString(resp, "token");
            }
            std::string profileResp = resp;
            if (!sessionCookie.empty()) {
                std::wstring profHeaders = L"Cookie: _nixlabs_session=" + Utf8ToWide(sessionCookie) +
                                           L"\r\nAuthorization: Bearer " + Utf8ToWide(sessionCookie) +
                                           L"\r\nAccept: application/json\r\n";
                int profStatus = 0;
                std::string pr = HttpRequestSync(L"accounts.nixlabs.tech", L"/api/profile", L"GET", "", profHeaders, &profStatus);
                if (profStatus == 200 && !pr.empty()) {
                    profileResp = pr;
                }
            }
            {
                std::lock_guard<std::mutex> lk(state.mu);
                state.isSubmittingAuth = false;
                state.isAuthenticated = true;
                state.sessionCookie = sessionCookie;
                state.user.name = identifier;
                state.user.handle = identifier;
                state.user.role = L"MEMBER";
                state.user.language = L"en-GB";
                ApplyUserJsonToStateLocked(state, profileResp);
                SaveAuthToDisk(state);
            }
            // Immediately fetch the connected Discord account & token from Nixlabs Accounts (matching Linux)
            FetchDiscordUserProfile(state, false);
        } else {
            std::lock_guard<std::mutex> lk(state.mu);
            state.isSubmittingAuth = false;
            std::string err = ExtractJsonString(resp, "error");
            state.authError = err.empty() ? L"Authentication failed." : Utf8ToWide(err);
            if (status == 0 || status >= 500) {
                state.hasPendingNotification = true;
                state.notifyTitle = L"Authentication Error";
                state.notifyMessage = state.authError;
                state.notifyFlags = 0x00000003; /* NIIF_ERROR */
            }
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void PerformLogout(AppState& state) {
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        cookie = state.sessionCookie;
        state.isAuthenticated = false;
        state.sessionCookie.clear();
        state.user = SessionUser{};
        state.discordUser = DiscordUser{};
        state.discordToken.clear();
        state.stateDirty.store(true, std::memory_order_release);
    }
    if (!cookie.empty() && cookie != "bench-session") {
        std::thread([cookie]() {
            std::wstring headers = L"Cookie: _nixlabs_session=" + Utf8ToWide(cookie) + L"\r\n";
            HttpRequestSync(L"accounts.nixlabs.tech", L"/api/logout", L"POST", "", headers, nullptr);
        }).detach();
    }
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
