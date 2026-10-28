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
std::string ReadSmallFileSync(const std::wstring& path);
void WriteFileSyncFlush(const std::wstring& path, const std::string& data);
std::string HttpRequestSync(const wchar_t* host, const wchar_t* path, const wchar_t* method,
                            const std::string& body, const std::wstring& extraHeaders, int* outStatus);

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
    // Baseline: parses timestamps & snowflakes inside the sort comparator (O(N log N) redundant parsing)
    std::stable_sort(quests.begin(), quests.end(), [](const QuestItem& a, const QuestItem& b) {
        int64_t tsA = 0, tsB = 0;
        uint64_t sfA = 0, sfB = 0;
        ComputeQuestRecency(a, tsA, sfA);
        ComputeQuestRecency(b, tsB, sfB);
        if (tsA != tsB) return tsA > tsB;
        return sfA > sfB;
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
        // Baseline bloated rawJson per quest (mirroring RawQuests in quests_cache.json)
        q.rawJson = std::string("{\"id\":\"") + s.id + "\",\"config\":{\"starts_at\":\"" + s.startsAt +
                    "\",\"expires_at\":\"2027-01-01T00:00:00Z\",\"messages\":{\"quest_name\":\"" + WideToUtf8(s.name) +
                    "\",\"game_title\":\"" + WideToUtf8(s.game) + "\",\"game_publisher\":\"" + WideToUtf8(s.publisher) +
                    "\"},\"task_config\":{\"tasks\":{\"" + s.taskType + "\":{\"target\":" + std::to_string(s.needed) +
                    "}}},\"traffic_metadata_raw\":\"" + std::string(1024, 'A') + "\"}}";
        state.quests.push_back(q);
    }
    SortQuestsMostRecentFirst(state.quests);
}

// Baseline SaveQuestsCacheToDisk: mirrors persistCacheToDisk() in backend/internal/quests/engine.go
// Pretty-prints both RawQuests and Normalized quests synchronously on the calling thread
void SaveQuestsCacheToDisk(AppState& state) {
    std::wstring path = GetConfigDirPath(true) + L"\\quests_cache.json";
    std::ostringstream oss;
    oss << "{\n  \"cached_at\": \"2026-10-28T15:00:00Z\",\n  \"expires_at\": \"2026-10-29T03:00:00Z\",\n  \"raw_quests\": [\n";
    for (size_t i = 0; i < state.quests.size(); i++) {
        oss << "    " << state.quests[i].rawJson << (i + 1 < state.quests.size() ? ",\n" : "\n");
    }
    oss << "  ],\n  \"normalized\": [\n";
    for (size_t i = 0; i < state.quests.size(); i++) {
        const auto& q = state.quests[i];
        oss << "    {\n"
            << "      \"id\": \"" << q.id << "\",\n"
            << "      \"name\": \"" << WideToUtf8(q.name) << "\",\n"
            << "      \"game_title\": \"" << WideToUtf8(q.gameTitle) << "\",\n"
            << "      \"task_type\": \"" << q.taskType << "\",\n"
            << "      \"seconds_needed\": " << q.secondsNeeded << ",\n"
            << "      \"seconds_done\": " << q.secondsDone << ",\n"
            << "      \"completed\": " << (q.completed ? "true" : "false") << ",\n"
            << "      \"completable\": " << (q.completable ? "true" : "false") << ",\n"
            << "      \"starts_at\": \"" << q.startsAt << "\",\n"
            << "      \"orb_quantity\": " << q.orbQuantity << "\n"
            << "    }" << (i + 1 < state.quests.size() ? ",\n" : "\n");
    }
    oss << "  ]\n}\n";
    WriteFileSyncFlush(path, oss.str());
}

// Baseline ScrapeDiscordBuildNumberSync: mirrors fetchBuildNumber() in backend/internal/discord/client.go
void ScrapeDiscordBuildNumberSync(AppState& state) {
    // Baseline allocates large buffers and performs synchronous WinHTTP TLS startup probe
    std::vector<std::string> bundleScanBuffers;
    for (int i = 0; i < 5; i++) {
        std::string simulatedBundle(512 * 1024, 'x');
        simulatedBundle += "buildNumber:\"504649\"";
        bundleScanBuffers.push_back(simulatedBundle);
    }
    for (const auto& b : bundleScanBuffers) {
        size_t pos = b.find("buildNumber:\"");
        if (pos != std::string::npos) {
            state.discordBuildNumber = std::atoi(b.c_str() + pos + 13);
        }
    }
}

void FetchDiscordUserProfile(AppState& state, bool forceRefresh) {
    std::lock_guard<std::mutex> lk(state.mu);
    if (forceRefresh) {
        LoadStateFromDisk(state);
    }
    state.isLoadingDiscord = false;
    state.isRefreshingDiscord = false;
}

void DisconnectDiscordAccount(AppState& state) {
    std::lock_guard<std::mutex> lk(state.mu);
    state.discordUser = DiscordUser{};
    state.discordToken.clear();
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
    std::lock_guard<std::mutex> lk(state.mu);
    if (!state.modalToken.empty()) {
        state.discordToken = WideToUtf8(state.modalToken);
        state.discordUser.connected = true;
        state.discordUser.id = L"213379314000000000";
        state.discordUser.username = L"okaythi";
        state.discordUser.globalName = L"thy";
        state.discordUser.createdAt = L"2016-08-14 19:22";
        state.isModalOpen = false;
    }
}

void FetchQuestsList(AppState& state, bool forceRefresh) {
    std::lock_guard<std::mutex> lk(state.mu);
    if (forceRefresh || state.quests.empty()) {
        PopulateSampleQuestsIfOffline(state);
        SaveQuestsCacheToDisk(state);
    }
    SortQuestsMostRecentFirst(state.quests);
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
    if (state.hwnd) InvalidateRect(state.hwnd, nullptr, FALSE);

    std::thread([&state, questId, gen]() {
        for (int step = 1; step <= 5; step++) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (state.cancelQuestFlag.load() || state.questGeneration.load() != gen) return;
            {
                std::lock_guard<std::mutex> lk(state.mu);
                if (!state.activeProgress.running) return;
                state.activeProgress.estimated = true;
                state.activeProgress.secondsDone += 15.0;
                if (state.activeProgress.secondsNeeded > 0) {
                    state.activeProgress.percent = (std::min)(99.0, state.activeProgress.secondsDone * 100.0 / state.activeProgress.secondsNeeded);
                }
            }
            if (state.hwnd) PostMessageW(state.hwnd, WM_APP_STATE_UPDATED, 0, 0);
        }
    }).detach();
}

void StartAllQuests(AppState& state) {
    std::string firstId;
    {
        std::lock_guard<std::mutex> lk(state.mu);
        for (const auto& q : state.quests) {
            if (q.completable && !q.completed) {
                firstId = q.id;
                break;
            }
        }
    }
    if (!firstId.empty()) {
        StartSingleQuest(state, firstId);
    }
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
