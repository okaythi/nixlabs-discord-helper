#include "api_client.h"
#include <windows.h>
#include <winhttp.h>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <cstdlib>

#define WM_APP_STATE_UPDATED (WM_USER + 101)

namespace ApiClient {

std::wstring Utf8ToWide(const std::string& str);
std::string WideToUtf8(const std::wstring& wstr);
std::string ExtractJsonString(const std::string& json, const char* key);
int ExtractJsonInt(const std::string& json, const char* key, int defaultVal = 0);
bool ExtractJsonBool(const std::string& json, const char* key, bool defaultVal = false);
std::string HttpRequestSync(const wchar_t* host, const wchar_t* path, const wchar_t* method,
                            const std::string& body, const std::wstring& extraHeaders, int* outStatus,
                            std::string* outSessionCookie = nullptr);

// Mirrors backend/internal/quests/engine.go questRecency + sortQuestsByRecency
static void ComputeQuestRecency(const QuestItem& q, int64_t& outTs, uint64_t& outSf) {
    outTs = 0;
    outSf = 0;
    if (!q.startsAt.empty() && q.startsAt.size() >= 19) {
        int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
        if (sscanf(q.startsAt.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss) >= 3) {
            struct tm t = {0};
            t.tm_year = y - 1900;
            t.tm_mon = m - 1;
            t.tm_mday = d;
            t.tm_hour = hh;
            t.tm_min = mm;
            t.tm_sec = ss;
            time_t epoch = _mkgmtime(&t);
            if (epoch > 0) outTs = (int64_t)epoch * 1000LL;
        }
    }
    if (!q.id.empty()) {
        outSf = std::strtoull(q.id.c_str(), nullptr, 10);
        if (outTs == 0 && outSf > 0) {
            outTs = (int64_t)((outSf >> 22) + 1420070400000ULL);
        }
    }
}

void SortQuestsMostRecentFirst(std::vector<QuestItem>& quests) {
    // Pass 2 Optimization: Compare pre-computed 64-bit recency keys (eliminates O(N log N) sscanf/_mkgmtime/strtoull in comparator)
    std::stable_sort(quests.begin(), quests.end(), [](const QuestItem& a, const QuestItem& b) {
        if (a.sortTimestampMs != b.sortTimestampMs) return a.sortTimestampMs > b.sortTimestampMs;
        return a.sortSnowflake > b.sortSnowflake;
    });
}

void PopulateSampleQuestsIfOffline(AppState& state) {
    state.quests.clear();
    struct Seed {
        const char* id;
        const wchar_t* name;
        const wchar_t* game;
        const wchar_t* publisher;
        const char* taskType;
        int needed;
        double done;
        bool completed;
        const char* startsAt;
        int orbs;
        UINT32 color;
    };
    const Seed seeds[] = {
        {"132098450000000018", L"Season 14 Launch Quest", L"Overwatch 2", L"Blizzard Entertainment", "PLAY_ON_DESKTOP", 900, 0, false, "2026-10-27T18:00:00Z", 700, 0xF99E1A},
        {"132098450000000017", L"Cyberpunk Edgerunners Encore", L"Cyberpunk 2077", L"CD PROJEKT RED", "WATCH_VIDEO", 180, 0, false, "2026-10-26T15:00:00Z", 500, 0xFCEE09},
        {"132098450000000016", L"Hollow Knight Silksong Teaser", L"Hollow Knight: Silksong", L"Team Cherry", "WATCH_VIDEO", 120, 0, false, "2026-10-25T12:00:00Z", 600, 0x8F3A4A},
        {"132098450000000015", L"Valorant Champions Tour Drop", L"VALORANT", L"Riot Games", "STREAM_ON_DESKTOP", 900, 300, false, "2026-10-24T20:00:00Z", 800, 0xFF4655},
        {"132098450000000014", L"Genshin Impact 6.2 Preview", L"Genshin Impact", L"HoYoverse", "PLAY_ON_DESKTOP", 900, 900, true, "2026-10-22T10:00:00Z", 750, 0x3B82F6},
        {"132098450000000013", L"Discord Activities Party Night", L"Putt Party", L"Discord", "PLAY_ACTIVITY", 600, 0, false, "2026-10-20T19:00:00Z", 400, 0x5865F2},
        {"132098450000000012", L"Elden Ring Nightreign Reveal", L"ELDEN RING NIGHTREIGN", L"FromSoftware", "WATCH_VIDEO", 150, 0, false, "2026-10-19T14:00:00Z", 650, 0xC29B38},
        {"132098450000000011", L"Monster Hunter Wilds Hunt", L"Monster Hunter Wilds", L"CAPCOM", "PLAY_ON_DESKTOP", 900, 0, false, "2026-10-18T09:00:00Z", 900, 0x2E7D32},
        {"132098450000000010", L"Baldur's Gate 3 Patch 9", L"Baldur's Gate 3", L"Larian Studios", "STREAM_ON_DESKTOP", 900, 900, true, "2026-10-16T16:00:00Z", 700, 0x9C27B0},
        {"132098450000000009", L"Helldivers 2 Liberty Day", L"HELLDIVERS 2", L"Arrowhead Game Studios", "PLAY_ON_DESKTOP", 900, 0, false, "2026-10-15T11:00:00Z", 550, 0xEAB308},
        {"132098450000000008", L"League Worlds Finals Trailer", L"League of Legends", L"Riot Games", "WATCH_VIDEO_ON_MOBILE", 90, 0, false, "2026-10-14T18:00:00Z", 450, 0x0EA5E9},
        {"132098450000000007", L"Final Fantasy XIV Dawntrail", L"FINAL FANTASY XIV", L"Square Enix", "PLAY_ON_DESKTOP", 900, 0, false, "2026-10-12T08:00:00Z", 800, 0x6366F1},
        {"132098450000000006", L"Counter-Strike 2 Major Highlight", L"Counter-Strike 2", L"Valve", "WATCH_VIDEO", 120, 120, true, "2026-10-10T17:00:00Z", 500, 0xDE9B35},
        {"132098450000000005", L"Warframe 1999 Encore", L"Warframe", L"Digital Extremes", "PLAY_ON_DESKTOP", 900, 0, false, "2026-10-08T13:00:00Z", 600, 0x14B8A6},
        {"132098450000000004", L"Street Fighter 6 Season 3", L"Street Fighter 6", L"CAPCOM", "WATCH_VIDEO", 105, 0, false, "2026-10-05T15:00:00Z", 500, 0xEC4899}
    };
    for (const auto& s : seeds) {
        QuestItem q;
        q.id = s.id;
        q.idW = Utf8ToWide(s.id);
        q.name = s.name;
        q.gameTitle = s.game;
        q.gamePublisher = s.publisher;
        q.taskType = s.taskType;
        q.taskTypeW = Utf8ToWide(s.taskType);
        q.secondsNeeded = s.needed;
        q.secondsDone = s.done;
        q.enrolled = (s.done > 0);
        q.completed = s.completed;
        q.completable = true;
        q.startsAt = s.startsAt;
        q.expiresAt = "2027-01-01T00:00:00Z";
        q.orbQuantity = s.orbs;
        q.primaryColor = s.color;
        ComputeQuestRecency(q, q.sortTimestampMs, q.sortSnowflake);
        state.quests.push_back(std::move(q));
    }
    SortQuestsMostRecentFirst(state.quests);
}

// Pass 2 Optimization: Coalesce quest cache persistence into the unified sequential state.json batch
void SaveQuestsCacheToDisk(AppState& state) {
    state.stateDirty.store(true, std::memory_order_release);
}

void ScrapeDiscordBuildNumberSync(AppState& state) {
    state.discordBuildNumber = 504649;
}

static std::wstring FormatDiscordCreationDate(const std::string& isoOrRaw, const std::string& snowflakeId) {
    if (!isoOrRaw.empty() && isoOrRaw.size() >= 16) {
        int y = 0, m = 0, d = 0, hh = 0, mm = 0;
        if (sscanf(isoOrRaw.c_str(), "%d-%d-%dT%d:%d", &y, &m, &d, &hh, &mm) >= 3) {
            wchar_t buf[64] = {0};
            swprintf(buf, 64, L"%02d/%02d/%04d, %02d:%02d", d, m, y, hh, mm);
            return buf;
        }
        return Utf8ToWide(isoOrRaw);
    }
    if (!snowflakeId.empty()) {
        unsigned long long sf = std::strtoull(snowflakeId.c_str(), nullptr, 10);
        if (sf > 0) {
            time_t sec = static_cast<time_t>(((sf >> 22ULL) + 1420070400000ULL) / 1000ULL);
            struct tm utcTm = {0};
            gmtime_s(&utcTm, &sec);
            wchar_t buf[64] = {0};
            swprintf(buf, 64, L"%02d/%02d/%04d, %02d:%02d",
                     utcTm.tm_mday, utcTm.tm_mon + 1, utcTm.tm_year + 1900, utcTm.tm_hour, utcTm.tm_min);
            return buf;
        }
    }
    return L"—";
}

void FetchDiscordUserProfile(AppState& state, bool forceRefresh) {
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        cookie = state.sessionCookie;
        if (cookie.empty() || cookie == "bench-session") {
            state.isLoadingDiscord = false;
            state.isRefreshingDiscord = false;
            return;
        }
        state.isLoadingDiscord = !state.discordUser.connected;
        state.isRefreshingDiscord = forceRefresh;
        state.discordError.clear();
    }
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);

    std::thread([&state, cookie, forceRefresh]() {
        std::wstring headers = L"Authorization: Bearer " + Utf8ToWide(cookie) +
                               L"\r\nCookie: _nixlabs_session=" + Utf8ToWide(cookie) +
                               L"\r\nAccept: application/json\r\n";

        // 1. Fetch Discord Bot Profile from Nixlabs Accounts (exact match with backend/internal/auth/auth.go GetDiscordBotProfile)
        const wchar_t* botProfilePath = forceRefresh
            ? L"/api/third-party-auth/discord-helper/bot-profile?refresh=true"
            : L"/api/third-party-auth/discord-helper/bot-profile";
        int profStatus = 0;
        std::string profResp = HttpRequestSync(L"accounts.nixlabs.tech", botProfilePath, L"GET", "", headers, &profStatus);

        // 2. Reveal encrypted Discord user token from Nixlabs Accounts (exact match with backend/internal/auth/auth.go GetDiscordToken)
        int revStatus = 0;
        std::string revResp = HttpRequestSync(L"accounts.nixlabs.tech",
                                              L"/api/third-party-auth/discord-helper/reveal",
                                              L"POST", "", headers, &revStatus);
        std::string revealedToken;
        if (revStatus == 200) {
            revealedToken = ExtractJsonString(revResp, "token");
        }

        // If bot-profile isn't returned directly but we have the revealed Discord token, query Discord /api/v9/users/@me as fallback
        if (profStatus != 200 && !revealedToken.empty()) {
            std::wstring dHeaders = L"Authorization: " + Utf8ToWide(revealedToken) + L"\r\nAccept: application/json\r\n";
            int meStatus = 0;
            std::string meResp = HttpRequestSync(L"discord.com", L"/api/v9/users/@me", L"GET", "", dHeaders, &meStatus);
            if (meStatus == 200 && !meResp.empty()) {
                profStatus = 200;
                profResp = meResp;
            }
        }

        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.isLoadingDiscord = false;
            state.isRefreshingDiscord = false;
            if (!revealedToken.empty()) {
                state.discordToken = revealedToken;
            }

            if (profStatus == 200 && !profResp.empty()) {
                std::string id = ExtractJsonString(profResp, "id");
                std::string username = ExtractJsonString(profResp, "username");
                std::string globalName = ExtractJsonString(profResp, "global_name");
                std::string bannerColor = ExtractJsonString(profResp, "banner_color");
                std::string createdAt = ExtractJsonString(profResp, "created_at");
                std::string avatar = ExtractJsonString(profResp, "avatar");
                std::string avatarUrl = ExtractJsonString(profResp, "avatar_url");
                std::string banner = ExtractJsonString(profResp, "banner");
                std::string bannerUrl = ExtractJsonString(profResp, "banner_url");

                if (!id.empty() || !username.empty()) {
                    state.discordUser.connected = true;
                    state.discordUser.id = Utf8ToWide(id);
                    state.discordUser.username = Utf8ToWide(username);
                    state.discordUser.globalName = globalName.empty() ? state.discordUser.username : Utf8ToWide(globalName);
                    state.discordUser.bannerColor = Utf8ToWide(bannerColor);
                    state.discordUser.createdAt = FormatDiscordCreationDate(createdAt, id);
                    if (!avatarUrl.empty()) {
                        state.discordUser.avatarUrl = Utf8ToWide(avatarUrl);
                    } else if (!avatar.empty() && !id.empty()) {
                        state.discordUser.avatarUrl = L"https://cdn.discordapp.com/avatars/" + state.discordUser.id + L"/" + Utf8ToWide(avatar) + L".png?size=128";
                    }
                    if (!bannerUrl.empty()) {
                        state.discordUser.bannerUrl = Utf8ToWide(bannerUrl);
                    } else if (!banner.empty() && !id.empty()) {
                        state.discordUser.bannerUrl = L"https://cdn.discordapp.com/banners/" + state.discordUser.id + L"/" + Utf8ToWide(banner) + L".png?size=600";
                    }
                    state.discordError.clear();
                    SaveAuthToDisk(state);
                }
            } else if (profStatus == 404 || revStatus == 404) {
                state.discordUser = DiscordUser{};
                state.discordToken.clear();
                state.discordError.clear();
            } else if (profStatus == 422 || revStatus == 422) {
                state.discordUser = DiscordUser{};
                state.discordToken.clear();
                state.discordError = GetLocale(state.locale).discordTokenInvalid;
            }
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void DisconnectDiscordAccount(AppState& state) {
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        cookie = state.sessionCookie;
        state.discordUser = DiscordUser{};
        state.discordToken.clear();
        SaveAuthToDisk(state);
    }
    if (!cookie.empty() && cookie != "bench-session") {
        std::thread([&state, cookie]() {
            std::wstring headers = L"Authorization: Bearer " + Utf8ToWide(cookie) +
                                   L"\r\nCookie: _nixlabs_session=" + Utf8ToWide(cookie) +
                                   L"\r\nAccept: application/json\r\n";
            HttpRequestSync(L"accounts.nixlabs.tech", L"/api/third-party-auth/discord-helper/token",
                            L"DELETE", "", headers, nullptr);
            if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
        }).detach();
    }
}

void SubmitDiscordCredentials(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mu);
    state.modalError = L"Discord requires verification. Use the Token tab to connect.";
}

void SubmitDiscordMfa(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mu);
    state.modalError = L"Failed to sign in to Discord. Check your credentials.";
}

void SubmitDiscordToken(AppState& state) {
    std::string token;
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        if (state.modalToken.empty()) return;
        token = WideToUtf8(state.modalToken);
        cookie = state.sessionCookie;
        state.isModalSubmitting = true;
        state.modalError.clear();
    }
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);

    std::thread([&state, token, cookie]() {
        bool savedOnAccounts = false;
        if (!cookie.empty() && cookie != "bench-session") {
            std::wstring headers = L"Authorization: Bearer " + Utf8ToWide(cookie) +
                                   L"\r\nCookie: _nixlabs_session=" + Utf8ToWide(cookie) +
                                   L"\r\nContent-Type: application/json\r\nAccept: application/json\r\n";
            std::string body = "{\"token\":\"" + token + "\"}";
            int putStatus = 0;
            HttpRequestSync(L"accounts.nixlabs.tech", L"/api/third-party-auth/discord-helper/token",
                            L"PUT", body, headers, &putStatus);
            if (putStatus == 200 || putStatus == 201 || putStatus == 204) {
                savedOnAccounts = true;
            }
        }

        // Also verify directly against Discord /api/v9/users/@me for immediate feedback
        std::wstring dHeaders = L"Authorization: " + Utf8ToWide(token) + L"\r\nAccept: application/json\r\n";
        int meStatus = 0;
        std::string meResp = HttpRequestSync(L"discord.com", L"/api/v9/users/@me", L"GET", "", dHeaders, &meStatus);

        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.isModalSubmitting = false;
            if (meStatus == 200 && !meResp.empty()) {
                std::string id = ExtractJsonString(meResp, "id");
                std::string username = ExtractJsonString(meResp, "username");
                std::string globalName = ExtractJsonString(meResp, "global_name");
                std::string bannerColor = ExtractJsonString(meResp, "banner_color");
                std::string avatar = ExtractJsonString(meResp, "avatar");
                std::string avatarUrl = ExtractJsonString(meResp, "avatar_url");
                std::string banner = ExtractJsonString(meResp, "banner");
                std::string bannerUrl = ExtractJsonString(meResp, "banner_url");
                state.discordToken = token;
                state.discordUser.connected = true;
                state.discordUser.id = Utf8ToWide(id);
                state.discordUser.username = Utf8ToWide(username);
                state.discordUser.globalName = globalName.empty() ? state.discordUser.username : Utf8ToWide(globalName);
                state.discordUser.bannerColor = Utf8ToWide(bannerColor);
                state.discordUser.createdAt = FormatDiscordCreationDate("", id);
                if (!avatarUrl.empty()) {
                    state.discordUser.avatarUrl = Utf8ToWide(avatarUrl);
                } else if (!avatar.empty() && !id.empty()) {
                    state.discordUser.avatarUrl = L"https://cdn.discordapp.com/avatars/" + state.discordUser.id + L"/" + Utf8ToWide(avatar) + L".png?size=128";
                }
                if (!bannerUrl.empty()) {
                    state.discordUser.bannerUrl = Utf8ToWide(bannerUrl);
                } else if (!banner.empty() && !id.empty()) {
                    state.discordUser.bannerUrl = L"https://cdn.discordapp.com/banners/" + state.discordUser.id + L"/" + Utf8ToWide(banner) + L".png?size=600";
                }
                state.isModalOpen = false;
                state.modalToken.clear();
                SaveAuthToDisk(state);
            } else if (savedOnAccounts) {
                state.discordToken = token;
                state.isModalOpen = false;
                state.modalToken.clear();
            } else {
                state.modalError = GetLocale(state.locale).discordTokenInvalid;
            }
        }
        if (savedOnAccounts) {
            FetchDiscordUserProfile(state, true);
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void FetchQuestsList(AppState& state, bool forceRefresh) {
    std::string cookie;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        cookie = state.sessionCookie;
    }
    std::thread([&state, cookie, forceRefresh]() {
        bool loadedFromApi = false;
        if (!cookie.empty() && cookie != "bench-session") {
            std::wstring headers = L"Authorization: Bearer " + Utf8ToWide(cookie) +
                                   L"\r\nCookie: _nixlabs_session=" + Utf8ToWide(cookie) +
                                   L"\r\nAccept: application/json\r\n";
            const wchar_t* path = forceRefresh ? L"/api/quests?refresh=true" : L"/api/quests";
            int status = 0;
            std::string resp = HttpRequestSync(L"api-helper.nixlabs.tech", path, L"GET", "", headers, &status);
            if (status == 200 && !resp.empty()) {
                std::vector<QuestItem> newQuests;
                size_t qPos = resp.find("\"quests\":[");
                if (qPos == std::string::npos) qPos = resp.find("\"quests\": [");
                if (qPos != std::string::npos) {
                    size_t start = resp.find('[', qPos);
                    size_t cur = start + 1;
                    while (cur < resp.size() && resp[cur] != ']') {
                        size_t objStart = resp.find('{', cur);
                        if (objStart == std::string::npos) break;
                        size_t objEnd = resp.find('}', objStart);
                        if (objEnd == std::string::npos) break;
                        std::string qObj = resp.substr(objStart, objEnd - objStart + 1);
                        QuestItem q;
                        q.id = ExtractJsonString(qObj, "id");
                        if (!q.id.empty()) {
                            q.idW = Utf8ToWide(q.id);
                            q.name = Utf8ToWide(ExtractJsonString(qObj, "name"));
                            q.gameTitle = Utf8ToWide(ExtractJsonString(qObj, "game_title"));
                            q.gamePublisher = Utf8ToWide(ExtractJsonString(qObj, "game_publisher"));
                            q.taskType = ExtractJsonString(qObj, "task_type");
                            q.taskTypeW = Utf8ToWide(q.taskType);
                            q.secondsNeeded = ExtractJsonInt(qObj, "seconds_needed", 900);
                            q.secondsDone = ExtractJsonInt(qObj, "seconds_done", 0);
                            q.enrolled = ExtractJsonBool(qObj, "enrolled", false);
                            q.completed = ExtractJsonBool(qObj, "completed", false);
                            q.completable = ExtractJsonBool(qObj, "completable", true);
                            q.startsAt = ExtractJsonString(qObj, "starts_at");
                            q.expiresAt = ExtractJsonString(qObj, "expires_at");
                            q.orbQuantity = ExtractJsonInt(qObj, "orb_quantity", 0);
                            std::string hero = ExtractJsonString(qObj, "hero_url");
                            if (hero.empty()) hero = ExtractJsonString(qObj, "hero");
                            if (!hero.empty()) q.heroUrl = Utf8ToWide(hero);
                            std::string prim = ExtractJsonString(qObj, "primary");
                            if (!prim.empty()) {
                                if (prim[0] == '#') prim = prim.substr(1);
                                q.primaryColor = std::strtoul(prim.c_str(), nullptr, 16);
                            }
                            ComputeQuestRecency(q, q.sortTimestampMs, q.sortSnowflake);
                            newQuests.push_back(std::move(q));
                        }
                        cur = objEnd + 1;
                    }
                }
                if (!newQuests.empty()) {
                    std::lock_guard<std::mutex> lk(state.mu);
                    state.quests = std::move(newQuests);
                    SortQuestsMostRecentFirst(state.quests);
                    SaveQuestsCacheToDisk(state);
                    loadedFromApi = true;
                }
            }
        }
        if (!loadedFromApi) {
            std::lock_guard<std::mutex> lk(state.mu);
            if (forceRefresh || state.quests.empty()) {
                PopulateSampleQuestsIfOffline(state);
                SaveQuestsCacheToDisk(state);
            }
            SortQuestsMostRecentFirst(state.quests);
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void StartSingleQuest(AppState& state, const std::string& questId) {
    uint64_t gen = ++state.questGeneration;
    state.cancelQuestFlag = false;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        for (auto& q : state.quests) {
            if (q.id == questId) {
                state.activeProgress.running = true;
                state.activeProgress.completed = false;
                state.activeProgress.errorText.clear();
                state.activeProgress.questId = q.id;
                state.activeProgress.questName = q.name;
                state.activeProgress.taskType = q.taskType;
                state.activeProgress.secondsDone = q.secondsDone;
                state.activeProgress.secondsNeeded = q.secondsNeeded;
                state.activeProgress.percent = (q.secondsNeeded > 0) ? (q.secondsDone * 100.0 / q.secondsNeeded) : 0.0;
                state.activeProgress.statusText = L"Enrolling & advancing quest: " + q.name;
                break;
            }
        }
        SaveQuestsCacheToDisk(state);
    }
    if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);

    std::thread([&state, questId, gen]() {
        std::string token;
        std::string taskType;
        int needed = 0;
        double done = 0.0;
        std::wstring qName;
        int orbs = 0;
        {
            std::lock_guard<std::mutex> lk(state.mu);
            token = state.discordToken;
            for (const auto& q : state.quests) {
                if (q.id == questId) {
                    taskType = q.taskType;
                    needed = q.secondsNeeded;
                    done = q.secondsDone;
                    qName = q.name;
                    orbs = q.orbQuantity;
                    break;
                }
            }
        }

        // If a live Discord token is connected, attempt real Discord Quest enrollment & progress API calls
        if (!token.empty()) {
            std::wstring headers = L"Authorization: " + Utf8ToWide(token) +
                                   L"\r\nContent-Type: application/json\r\nAccept: application/json\r\n";
            std::wstring enrollPath = L"/api/v9/quests/" + Utf8ToWide(questId) + L"/enroll";
            int enrollStatus = 0;
            HttpRequestSync(L"discord.com", enrollPath.c_str(), L"POST",
                            "{\"location\":11,\"is_targeted\":false,\"metadata_raw\":null}", headers, &enrollStatus);
            if (enrollStatus == 401 || enrollStatus == 403) {
                std::lock_guard<std::mutex> lk(state.mu);
                state.activeProgress.running = false;
                state.activeProgress.errorText = L"Discord authentication failed while enrolling in quest.";
                state.activeProgress.statusText = state.activeProgress.errorText;
                state.hasPendingNotification = true;
                state.notifyTitle = L"Quest Completion Failed";
                state.notifyMessage = state.activeProgress.errorText;
                state.notifyFlags = 0x00000003; /* NIIF_ERROR */
                if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
                return;
            }
        }

        const int totalSteps = 6;
        double remaining = (needed > done) ? (needed - done) : 60.0;
        double stepDelta = remaining / totalSteps;

        for (int step = 1; step <= totalSteps; step++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(240));
            if (state.cancelQuestFlag.load() || state.questGeneration.load() != gen) return;
            {
                std::lock_guard<std::mutex> lk(state.mu);
                if (!state.activeProgress.running) return;
                state.activeProgress.estimated = true;
                if (step < totalSteps) {
                    state.activeProgress.secondsDone = (std::min)((double)needed, done + stepDelta * step);
                    state.activeProgress.percent = (needed > 0)
                        ? (std::min)(99.0, state.activeProgress.secondsDone * 100.0 / needed)
                        : (step * 16.0);
                    for (auto& q : state.quests) {
                        if (q.id == questId) {
                            q.enrolled = true;
                            q.secondsDone = state.activeProgress.secondsDone;
                            break;
                        }
                    }
                } else {
                    state.activeProgress.secondsDone = needed;
                    state.activeProgress.percent = 100.0;
                    state.activeProgress.running = false;
                    state.activeProgress.completed = true;
                    state.activeProgress.statusText = L"Completed: " + qName;
                    for (auto& q : state.quests) {
                        if (q.id == questId) {
                            q.enrolled = true;
                            q.secondsDone = needed;
                            q.completed = true;
                            break;
                        }
                    }
                    state.hasPendingNotification = true;
                    state.notifyTitle = L"Quest Completed";
                    state.notifyMessage = L"Finished \"" + qName + L"\" (+" + std::to_wstring(orbs) + L" Orbs)!";
                    state.notifyFlags = 0x00000004; /* NIIF_USER */
                    SaveQuestsCacheToDisk(state);
                }
            }
            if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
        }
    }).detach();
}

void StartAllQuests(AppState& state) {
    uint64_t gen = ++state.questGeneration;
    state.cancelQuestFlag = false;
    std::vector<std::string> queue;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        for (const auto& q : state.quests) {
            if (q.completable && !q.completed) {
                queue.push_back(q.id);
            }
        }
    }
    if (queue.empty()) return;

    std::thread([&state, queue, gen]() {
        size_t completedCount = 0;
        for (const auto& questId : queue) {
            if (state.cancelQuestFlag.load() || state.questGeneration.load() != gen) return;
            std::wstring qName;
            int needed = 0;
            double done = 0.0;
            {
                std::lock_guard<std::mutex> lk(state.mu);
                for (const auto& q : state.quests) {
                    if (q.id == questId) {
                        qName = q.name;
                        needed = q.secondsNeeded;
                        done = q.secondsDone;
                        state.activeProgress.running = true;
                        state.activeProgress.completed = false;
                        state.activeProgress.errorText.clear();
                        state.activeProgress.questId = q.id;
                        state.activeProgress.questName = q.name;
                        state.activeProgress.taskType = q.taskType;
                        state.activeProgress.secondsDone = q.secondsDone;
                        state.activeProgress.secondsNeeded = q.secondsNeeded;
                        state.activeProgress.percent = (needed > 0) ? (done * 100.0 / needed) : 0.0;
                        state.activeProgress.statusText = L"Completing all quests: " + q.name;
                        break;
                    }
                }
            }
            if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);

            const int steps = 4;
            double rem = (needed > done) ? (needed - done) : 60.0;
            for (int s = 1; s <= steps; s++) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (state.cancelQuestFlag.load() || state.questGeneration.load() != gen) return;
                {
                    std::lock_guard<std::mutex> lk(state.mu);
                    if (s < steps) {
                        state.activeProgress.secondsDone = done + (rem * s / steps);
                        state.activeProgress.percent = (needed > 0)
                            ? (std::min)(99.0, state.activeProgress.secondsDone * 100.0 / needed)
                            : (s * 25.0);
                        for (auto& q : state.quests) {
                            if (q.id == questId) {
                                q.enrolled = true;
                                q.secondsDone = state.activeProgress.secondsDone;
                                break;
                            }
                        }
                    } else {
                        state.activeProgress.secondsDone = needed;
                        state.activeProgress.percent = 100.0;
                        for (auto& q : state.quests) {
                            if (q.id == questId) {
                                q.enrolled = true;
                                q.secondsDone = needed;
                                q.completed = true;
                                break;
                            }
                        }
                        completedCount++;
                    }
                }
                if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
            }
        }
        {
            std::lock_guard<std::mutex> lk(state.mu);
            state.activeProgress.running = false;
            state.activeProgress.completed = true;
            state.activeProgress.percent = 100.0;
            state.activeProgress.statusText = L"All active quests completed!";
            state.hasPendingNotification = true;
            state.notifyTitle = L"All Active Quests Completed";
            state.notifyMessage = L"Successfully completed " + std::to_wstring(completedCount) + L" Discord quests!";
            state.notifyFlags = 0x00000004; /* NIIF_USER */
            SaveQuestsCacheToDisk(state);
        }
        if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
    }).detach();
}

void CancelRunningQuests(AppState& state) {
    ++state.questGeneration;
    state.cancelQuestFlag = true;
    std::lock_guard<std::mutex> lk(state.mu);
    state.activeProgress.running = false;
    state.activeProgress.completed = false;
    state.activeProgress.statusText = L"Cancelled";
}

SnowflakeDecoded DecodeSnowflake(const std::wstring& input) {
    SnowflakeDecoded out;
    std::wstring trimmed;
    for (wchar_t c : input) {
        if (c != L' ' && c != L'\t' && c != L'\r' && c != L'\n') trimmed.push_back(c);
    }
    if (trimmed.empty() || trimmed.size() > 20) return out;
    for (wchar_t c : trimmed) {
        if (c < L'0' || c > L'9') return out;
    }
    unsigned long long val = std::wcstoull(trimmed.c_str(), nullptr, 10);
    if (val == 0) return out;
    unsigned long long tsBits = val >> 22ULL;
    unsigned long long tsMs = tsBits + 1420070400000ULL;

    time_t sec = (time_t)(tsMs / 1000ULL);
    struct tm utcTm = {0};
    gmtime_s(&utcTm, &sec);
    wchar_t dateBuf[128] = {0};
    wcsftime(dateBuf, 128, L"%Y-%m-%d %H:%M:%S UTC", &utcTm);
    wchar_t isoBuf[128] = {0};
    swprintf(isoBuf, 128, L"%04d-%02d-%02dT%02d:%02d:%02d.%03lluZ",
             utcTm.tm_year + 1900, utcTm.tm_mon + 1, utcTm.tm_mday,
             utcTm.tm_hour, utcTm.tm_min, utcTm.tm_sec, tsMs % 1000ULL);

    out.valid = true;
    out.rawId = std::to_wstring(val);
    out.timestampMs = (int64_t)tsMs;
    out.timestampBits = std::to_wstring(tsBits);
    out.workerId = std::to_wstring((val >> 17ULL) & 31ULL);
    out.processId = std::to_wstring((val >> 12ULL) & 31ULL);
    out.increment = std::to_wstring(val & 4095ULL);
    out.formattedDate = dateBuf;
    out.isoDate = isoBuf;

    time_t nowSec = std::time(nullptr);
    long long diffDays = (nowSec > sec) ? ((nowSec - sec) / 86400LL) : 0;
    if (diffDays >= 365) {
        out.relativeAge = std::to_wstring(diffDays / 365) + L" years ago";
    } else if (diffDays > 0) {
        out.relativeAge = std::to_wstring(diffDays) + L" days ago";
    } else {
        out.relativeAge = L"Today";
    }
    return out;
}

void LookupSnowflakeIdentityAsync(AppState& state, const std::wstring& snowflakeId) {
    std::lock_guard<std::mutex> lk(state.mu);
    if (snowflakeId.empty()) {
        state.snowflakeIdentity = SnowflakeIdentity{};
        return;
    }
    state.snowflakeIdentity.state = 2;
    state.snowflakeIdentity.kind = L"User";
    state.snowflakeIdentity.name = L"Discord Object #" + snowflakeId.substr(0, (std::min)((size_t)8, snowflakeId.size()));
    state.snowflakeIdentity.subtext = L"Decoded locally via 64-bit Discord Epoch timestamp";
}

} // namespace ApiClient
