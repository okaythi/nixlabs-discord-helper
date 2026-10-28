#pragma once
#include "app_state.h"

namespace ApiClient {
    const std::wstring& GetConfigDirPath();
    void LoadStateFromDisk(AppState& state);
    void SaveAuthToDisk(AppState& state);
    void SaveQuestsCacheToDisk(AppState& state);
    void FlushStateIfDirty(AppState& state);

    void StartInitialSessionCheck(AppState& state);
    void PerformLogin(AppState& state, const std::wstring& identifier, const std::wstring& password);
    void PerformLogout(AppState& state);
    void OpenCreateAccountInBrowser(AppState& state);

    void FetchDiscordUserProfile(AppState& state, bool forceRefresh);
    void DisconnectDiscordAccount(AppState& state);
    void SubmitDiscordCredentials(AppState& state);
    void SubmitDiscordMfa(AppState& state);
    void SubmitDiscordToken(AppState& state);

    void FetchQuestsList(AppState& state, bool forceRefresh);
    void StartSingleQuest(AppState& state, const std::string& questId);
    void StartAllQuests(AppState& state);
    void CancelRunningQuests(AppState& state);

    SnowflakeDecoded DecodeSnowflake(const std::wstring& input);
    void LookupSnowflakeIdentityAsync(AppState& state, const std::wstring& snowflakeId);
    void ScrapeDiscordBuildNumberSync(AppState& state);
    void PopulateSampleQuestsIfOffline(AppState& state);
}
