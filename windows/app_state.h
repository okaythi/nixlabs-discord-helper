#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include "i18n.h"

enum class NavTab {
    Account = 0,
    Quests = 1,
    Snowflakes = 2
};

enum class LoginMode {
    Card = 0,
    InAppForm = 1,
    AwaitingBrowser = 2
};

enum class ModalTab {
    Credentials = 0,
    Token = 1
};

enum class FocusedInput {
    None = 0,
    LoginIdentifier,
    LoginPassword,
    SnowflakeId,
    ModalEmail,
    ModalPassword,
    ModalMfaCode,
    ModalToken
};

struct SessionUser {
    std::wstring id;
    std::wstring name;
    std::wstring handle;
    std::wstring email;
    std::wstring avatarUrl;
    std::wstring role;
    std::wstring language;
};

struct NixlabsAccount {
    int accountStanding = 0;
    bool banned = false;
};

struct DiscordUser {
    bool connected = false;
    std::wstring id;
    std::wstring username;
    std::wstring globalName;
    std::wstring avatarUrl;
    std::wstring bannerUrl;
    std::wstring bannerColor;
    std::wstring createdAt;
};

struct QuestItem {
    std::string id;
    std::wstring idW;
    std::wstring name;
    std::wstring gameTitle;
    std::wstring gamePublisher;
    std::string taskType;
    std::wstring taskTypeW;
    int secondsNeeded = 0;
    double secondsDone = 0.0;
    bool enrolled = false;
    bool completed = false;
    bool completable = false;
    std::string startsAt;
    std::string expiresAt;
    int orbQuantity = 0;
    std::wstring heroUrl;
    UINT32 primaryColor = 0x4752C4;
    int64_t sortTimestampMs = 0;
    uint64_t sortSnowflake = 0;
    std::string rawJson;
};

struct QuestProgress {
    bool running = false;
    bool completed = false;
    bool estimated = false;
    std::string questId;
    std::wstring questName;
    std::string taskType;
    double secondsDone = 0.0;
    int secondsNeeded = 0;
    double percent = 0.0;
    std::wstring statusText;
    std::wstring errorText;
};

struct SnowflakeDecoded {
    bool valid = false;
    std::wstring rawId;
    int64_t timestampMs = 0;
    std::wstring timestampBits;
    std::wstring workerId;
    std::wstring processId;
    std::wstring increment;
    std::wstring formattedDate;
    std::wstring isoDate;
    std::wstring relativeAge;
};

struct SnowflakeIdentity {
    int state = 0; // 0 = idle, 1 = loading, 2 = resolved, 3 = unavailable/unknown
    std::wstring kind; // user, server, channel, unknown
    std::wstring name;
    std::wstring subtext;
    std::wstring extra;
};

struct ClickRect {
    int id;
    RECT rc;
};

struct AppState {
    std::mutex mu;
    HWND hwnd = nullptr;

    // Theme & Locale
    bool darkMode = true;
    LocaleId locale = LocaleId::EN_UK;

    // Auth & Session
    bool isLoadingAuth = true;
    bool isAuthenticated = false;
    bool isDisconnected = false;
    bool isSubmittingAuth = false;
    std::string sessionCookie;
    std::string authUpdatedAt;
    SessionUser user;
    NixlabsAccount account;
    LoginMode loginMode = LoginMode::Card;
    std::wstring loginIdentifier;
    std::wstring loginPassword;
    std::wstring authError;
    std::wstring registerUrl;

    // Navigation & Scrolling
    NavTab activeTab = NavTab::Account;
    int scrollY = 0;
    int maxScrollY = 0;

    // Account View & Discord Profile
    bool isLoadingDiscord = false;
    bool isRefreshingDiscord = false;
    DiscordUser discordUser;
    std::wstring discordError;

    // Connect Discord Modal
    bool isModalOpen = false;
    ModalTab modalTab = ModalTab::Credentials;
    bool isModalSubmitting = false;
    bool modalMfaRequired = false;
    std::string modalMfaTicket;
    std::wstring modalEmail;
    std::wstring modalPassword;
    std::wstring modalMfaCode;
    std::wstring modalToken;
    std::wstring modalError;
    std::wstring modalSuccess;

    // Quests View
    bool isLoadingQuests = false;
    bool isRefreshingQuests = false;
    std::vector<QuestItem> quests;
    int visibleQuestCount = 12;
    std::wstring questsError;
    QuestProgress activeProgress;
    std::atomic<bool> cancelQuestFlag{false};
    std::atomic<uint64_t> questGeneration{0};
    int discordBuildNumber = 504649;
    std::string discordToken;

    // Snowflakes View
    std::wstring snowflakeInput;
    SnowflakeDecoded snowflakeDecoded;
    SnowflakeIdentity snowflakeIdentity;
    int copiedFieldId = -1;

    // Input & Hit-testing
    FocusedInput focusedInput = FocusedInput::None;
    std::vector<ClickRect> clickTargets;
    int hoveredTargetId = -1;

    // Diagnostics / Bench Mode
    bool benchMode = false;
    std::atomic<bool> stateDirty{false};
};
