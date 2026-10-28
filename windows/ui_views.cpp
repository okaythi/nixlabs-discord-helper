#include "ui_renderer.h"
#include "i18n.h"
#include <algorithm>

void UiRenderer::RenderLoginScreen(AppState& state, float W, float H) {
    const auto& L = GetLocale(state.locale);
    UINT32 cardBg = state.darkMode ? 0x252628 : 0xFFFFFF;
    UINT32 border = state.darkMode ? 0x42454A : 0xDCE1E7;
    UINT32 text = state.darkMode ? 0xE8EAED : 0x202124;
    UINT32 muted = state.darkMode ? 0xADB2B9 : 0x5C626B;
    UINT32 accent = state.darkMode ? 0xA8CDFF : 0x0B57C3;
    UINT32 onAccent = state.darkMode ? 0x10335E : 0xFFFFFF;
    UINT32 inputBg = state.darkMode ? 0x1E1F20 : 0xF8FAFD;

    float cardW = (std::min)(420.0f, W - 48.0f);
    float cardH = (state.loginMode == LoginMode::InAppForm) ? 350.0f : 260.0f;
    float cardX = (W - cardW) * 0.5f;
    float cardY = (H - cardH) * 0.5f;

    FillRoundRect(cardX, cardY, cardW, cardH, 24.0f, cardBg);
    DrawRoundRect(cardX, cardY, cardW, cardH, 24.0f, border);

    DrawTextStr(std::wstring(L.brandName) + L" | " + L.productName, m_fmtWordmark, cardX + 36.0f, cardY + 32.0f, cardW - 72.0f, 30.0f, text);
    DrawTextStr(L.yourAccount, m_fmtTitle, cardX + 36.0f, cardY + 72.0f, cardW - 72.0f, 36.0f, text);

    if (!state.authError.empty()) {
        FillRoundRect(cardX + 36.0f, cardY + 112.0f, cardW - 72.0f, 32.0f, 8.0f, 0x422F30);
        DrawTextStr(state.authError, m_fmtSmall, cardX + 46.0f, cardY + 119.0f, cardW - 92.0f, 20.0f, 0xFFB4AB);
    }

    if (state.loginMode == LoginMode::Card) {
        FillRoundRect(cardX + 36.0f, cardY + 140.0f, cardW - 72.0f, 40.0f, 20.0f, accent);
        DrawTextStr(L.signIn, m_fmtBodyBold, cardX + 60.0f, cardY + 150.0f, cardW - 120.0f, 24.0f, onAccent);
        RegisterHit(state, HIT_LOGIN_SHOW_FORM, cardX + 36.0f, cardY + 140.0f, cardW - 72.0f, 40.0f);

        DrawRoundRect(cardX + 36.0f, cardY + 192.0f, cardW - 72.0f, 40.0f, 20.0f, border);
        DrawTextStr(L.createAccount, m_fmtBodyBold, cardX + 60.0f, cardY + 202.0f, cardW - 120.0f, 24.0f, accent);
        RegisterHit(state, HIT_LOGIN_CREATE_ACCOUNT, cardX + 36.0f, cardY + 192.0f, cardW - 72.0f, 40.0f);
    } else {
        DrawTextStr(L.signInPrompt, m_fmtSmall, cardX + 36.0f, cardY + 108.0f, cardW - 72.0f, 20.0f, muted);

        FillRoundRect(cardX + 36.0f, cardY + 136.0f, cardW - 72.0f, 38.0f, 8.0f, inputBg);
        DrawRoundRect(cardX + 36.0f, cardY + 136.0f, cardW - 72.0f, 38.0f, 8.0f,
                      state.focusedInput == FocusedInput::LoginIdentifier ? accent : border);
        DrawTextStr(state.loginIdentifier.empty() ? L.identifierPlaceholder : state.loginIdentifier,
                    m_fmtBody, cardX + 48.0f, cardY + 145.0f, cardW - 96.0f, 22.0f,
                    state.loginIdentifier.empty() ? muted : text);
        RegisterHit(state, HIT_LOGIN_IDENTIFIER_INPUT, cardX + 36.0f, cardY + 136.0f, cardW - 72.0f, 38.0f);

        std::wstring maskedPass(state.loginPassword.size(), L'\x2022');
        FillRoundRect(cardX + 36.0f, cardY + 186.0f, cardW - 72.0f, 38.0f, 8.0f, inputBg);
        DrawRoundRect(cardX + 36.0f, cardY + 186.0f, cardW - 72.0f, 38.0f, 8.0f,
                      state.focusedInput == FocusedInput::LoginPassword ? accent : border);
        DrawTextStr(maskedPass.empty() ? L.passwordPlaceholder : maskedPass,
                    m_fmtBody, cardX + 48.0f, cardY + 195.0f, cardW - 96.0f, 22.0f,
                    maskedPass.empty() ? muted : text);
        RegisterHit(state, HIT_LOGIN_PASSWORD_INPUT, cardX + 36.0f, cardY + 186.0f, cardW - 72.0f, 38.0f);

        FillRoundRect(cardX + 36.0f, cardY + 240.0f, cardW - 72.0f, 40.0f, 20.0f, accent);
        DrawTextStr(state.isSubmittingAuth ? L.signingIn : L.signIn, m_fmtBodyBold, cardX + 60.0f, cardY + 250.0f, cardW - 120.0f, 24.0f, onAccent);
        RegisterHit(state, HIT_LOGIN_SUBMIT, cardX + 36.0f, cardY + 240.0f, cardW - 72.0f, 40.0f);

        DrawTextStr(L.back, m_fmtBody, cardX + 36.0f, cardY + 292.0f, 100.0f, 24.0f, muted);
        RegisterHit(state, HIT_LOGIN_BACK, cardX + 36.0f, cardY + 288.0f, 100.0f, 28.0f);
    }
}

void UiRenderer::RenderHeaderAndSidebar(AppState& state, float W, float H) {
    const auto& L = GetLocale(state.locale);
    UINT32 bg = state.darkMode ? 0x1E1F20 : 0xF8FAFD;
    UINT32 subtle = state.darkMode ? 0x2C2E31 : 0xE9EEF5;
    UINT32 border = state.darkMode ? 0x42454A : 0xDCE1E7;
    UINT32 text = state.darkMode ? 0xE8EAED : 0x202124;
    UINT32 muted = state.darkMode ? 0xADB2B9 : 0x5C626B;
    UINT32 activeBg = state.darkMode ? 0x193F61 : 0xD8E7FF;
    UINT32 activeText = state.darkMode ? 0xD6EAFF : 0x174A8B;

    FillRoundRect(0.0f, 0.0f, W, 72.0f, 0.0f, bg);
    DrawTextStr(L.brandName, m_fmtWordmark, 28.0f, 22.0f, 80.0f, 30.0f, text);
    FillRoundRect(108.0f, 26.0f, 1.0f, 20.0f, 0.0f, border);
    DrawTextStr(L.productName, m_fmtCardTitle, 122.0f, 24.0f, 200.0f, 28.0f, muted);

    // Locale switch pill
    const wchar_t* locCodes[] = {L"EN-UK", L"EN-US", L"FR-BE", L"JA", L"PT-BR", L"PT-PT"};
    FillRoundRect(W - 310.0f, 20.0f, 64.0f, 32.0f, 16.0f, subtle);
    DrawRoundRect(W - 310.0f, 20.0f, 64.0f, 32.0f, 16.0f, border);
    DrawTextStr(locCodes[(int)state.locale], m_fmtSmall, W - 296.0f, 28.0f, 48.0f, 20.0f, text);
    RegisterHit(state, HIT_LOCALE_CYCLE, W - 310.0f, 20.0f, 64.0f, 32.0f);

    // Theme button
    FillRoundRect(W - 236.0f, 18.0f, 36.0f, 36.0f, 18.0f, subtle);
    DrawTextStr(state.darkMode ? L"\x2600" : L"\x263E", m_fmtBodyBold, W - 225.0f, 26.0f, 24.0f, 24.0f, text);
    RegisterHit(state, HIT_THEME_TOGGLE, W - 236.0f, 18.0f, 36.0f, 36.0f);

    // User badge
    FillRoundRect(W - 188.0f, 18.0f, 160.0f, 36.0f, 18.0f, subtle);
    DrawRoundRect(W - 188.0f, 18.0f, 160.0f, 36.0f, 18.0f, border);
    DrawTextStr(L"@" + (state.user.handle.empty() ? L"user" : state.user.handle), m_fmtSmall, W - 172.0f, 28.0f, 100.0f, 20.0f, text);
    DrawTextStr(L"\x21AA", m_fmtBodyBold, W - 54.0f, 26.0f, 24.0f, 24.0f, muted);
    RegisterHit(state, HIT_LOGOUT, W - 60.0f, 18.0f, 32.0f, 36.0f);

    // Sidebar
    if (W > 760.0f) {
        FillRoundRect(0.0f, 72.0f, 244.0f, H - 72.0f, 0.0f, bg);
        struct TabDef { NavTab tab; int hit; const wchar_t* label; UINT32 dotColor; };
        TabDef tabs[] = {
            {NavTab::Account, HIT_TAB_ACCOUNT, L.tabAccount, 0xA8C7FA},
            {NavTab::Quests, HIT_TAB_QUESTS, L.tabQuests, 0xCBB7F2},
            {NavTab::Snowflakes, HIT_TAB_SNOWFLAKES, L.tabSnowflakes, 0x91D4F6}
        };
        float y = 96.0f;
        for (const auto& t : tabs) {
            bool active = (state.activeTab == t.tab);
            if (active) FillRoundRect(18.0f, y, 212.0f, 44.0f, 22.0f, activeBg);
            FillRoundRect(26.0f, y + 6.0f, 32.0f, 32.0f, 16.0f, t.dotColor);
            DrawTextStr(t.label, m_fmtBodyBold, 70.0f, y + 12.0f, 150.0f, 24.0f, active ? activeText : text);
            RegisterHit(state, t.hit, 18.0f, y, 212.0f, 44.0f);
            y += 52.0f;
        }
        DrawTextStr(L.ecosystemLink, m_fmtSmall, 32.0f, H - 44.0f, 140.0f, 20.0f, muted);
        RegisterHit(state, HIT_ECOSYSTEM_LINK, 32.0f, H - 48.0f, 140.0f, 24.0f);
    }
}

void UiRenderer::RenderAccountTab(AppState& state, float x, float y, float w) {
    const auto& L = GetLocale(state.locale);
    UINT32 cardBg = state.darkMode ? 0x252628 : 0xFFFFFF;
    UINT32 border = state.darkMode ? 0x42454A : 0xDCE1E7;
    UINT32 text = state.darkMode ? 0xE8EAED : 0x202124;
    UINT32 muted = state.darkMode ? 0xADB2B9 : 0x5C626B;

    // Discord Hero Card
    FillRoundRect(x, y, w, 280.0f, 16.0f, cardBg);
    FillRoundRect(x, y, w, 110.0f, 16.0f, state.discordUser.connected ? 0x5865F2 : 0x323436);
    DrawRoundRect(x, y, w, 280.0f, 16.0f, border);

    FillRoundRect(x + w - 98.0f, y + 14.0f, 82.0f, 26.0f, 12.0f, 0x101214, 0.7f);
    DrawTextStr(L"Discord", m_fmtSmall, x + w - 82.0f, y + 19.0f, 64.0f, 18.0f, 0xFFFFFF);

    FillRoundRect(x + 24.0f, y + 74.0f, 72.0f, 72.0f, 36.0f, 0x5865F2);
    DrawTextStr(state.discordUser.connected ? L"D" : L"?", m_fmtTitle, x + 50.0f, y + 92.0f, 36.0f, 36.0f, 0xFFFFFF);

    DrawTextStr(state.discordUser.connected ? state.discordUser.globalName : L.discordNotConnected,
                m_fmtTitle, x + 24.0f, y + 156.0f, w - 120.0f, 32.0f, text);
    DrawTextStr(state.discordUser.connected ? (L"@" + state.discordUser.username) : L.discordConnectPrompt,
                m_fmtBody, x + 24.0f, y + 190.0f, w - 48.0f, 24.0f, muted);

    if (!state.discordUser.connected) {
        FillRoundRect(x + 24.0f, y + 224.0f, 176.0f, 38.0f, 19.0f, 0x5865F2);
        DrawTextStr(L.connectDiscord, m_fmtBodyBold, x + 46.0f, y + 233.0f, 140.0f, 22.0f, 0xFFFFFF);
        RegisterHit(state, HIT_DISCORD_CONNECT_BTN, x + 24.0f, y + 224.0f, 176.0f, 38.0f);
    } else {
        DrawTextStr(std::wstring(L.creationDateLabel) + L": " + state.discordUser.createdAt, m_fmtSmall, x + 24.0f, y + 232.0f, 260.0f, 20.0f, text);
        DrawTextStr(std::wstring(L.userIdLabel) + L": " + state.discordUser.id, m_fmtMono, x + 300.0f, y + 232.0f, 260.0f, 20.0f, text);
        DrawTextStr(L.discordDisconnect, m_fmtSmall, x + w - 110.0f, y + 232.0f, 90.0f, 20.0f, 0xFFB4AB);
        RegisterHit(state, HIT_DISCORD_DISCONNECT_BTN, x + w - 115.0f, y + 226.0f, 100.0f, 28.0f);
    }

    // Nixlabs Profile Card
    float ny = y + 304.0f;
    FillRoundRect(x, ny, w, 190.0f, 16.0f, cardBg);
    DrawRoundRect(x, ny, w, 190.0f, 16.0f, border);
    DrawTextStr(L.nixlabsProfileTitle, m_fmtCardTitle, x + 24.0f, ny + 22.0f, 300.0f, 26.0f, text);
    DrawTextStr(state.user.name + L" (@" + state.user.handle + L")", m_fmtBodyBold, x + 24.0f, ny + 62.0f, 340.0f, 24.0f, text);

    FillRoundRect(x + w - 144.0f, ny + 58.0f, 120.0f, 26.0f, 13.0f, 0x1E3A2B);
    DrawTextStr(L.standingGood, m_fmtSmall, x + w - 130.0f, ny + 63.0f, 100.0f, 18.0f, 0x8CD6A7);

    DrawTextStr(std::wstring(L.emailLabel) + L": " + state.user.email, m_fmtSmall, x + 24.0f, ny + 120.0f, 280.0f, 20.0f, muted);
    DrawTextStr(std::wstring(L.roleLabel) + L": " + state.user.role, m_fmtSmall, x + 320.0f, ny + 120.0f, 160.0f, 20.0f, muted);
    DrawTextStr(std::wstring(L.languageLabel) + L": " + state.user.language, m_fmtSmall, x + 500.0f, ny + 120.0f, 180.0f, 20.0f, muted);
}

void UiRenderer::RenderQuestsTab(AppState& state, float x, float y, float w) {
    const auto& L = GetLocale(state.locale);
    UINT32 cardBg = state.darkMode ? 0x252628 : 0xFFFFFF;
    UINT32 border = state.darkMode ? 0x42454A : 0xDCE1E7;
    UINT32 text = state.darkMode ? 0xE8EAED : 0x202124;
    UINT32 muted = state.darkMode ? 0xADB2B9 : 0x5C626B;
    UINT32 accent = state.darkMode ? 0xA8CDFF : 0x0B57C3;
    UINT32 onAccent = state.darkMode ? 0x10335E : 0xFFFFFF;

    DrawTextStr(L.questsTitle, m_fmtTitle, x, y, 360.0f, 32.0f, text);
    DrawTextStr(L.questsSubtitle, m_fmtBody, x, y + 34.0f, 480.0f, 22.0f, muted);

    DrawRoundRect(x + w - 340.0f, y + 8.0f, 118.0f, 36.0f, 18.0f, border);
    DrawTextStr(L.refreshQuests, m_fmtSmall, x + w - 322.0f, y + 18.0f, 96.0f, 20.0f, accent);
    RegisterHit(state, HIT_QUESTS_REFRESH, x + w - 340.0f, y + 8.0f, 118.0f, 36.0f);

    FillRoundRect(x + w - 210.0f, y + 8.0f, 210.0f, 36.0f, 18.0f, accent);
    DrawTextStr(L.completeAll, m_fmtSmall, x + w - 196.0f, y + 18.0f, 186.0f, 20.0f, onAccent);
    RegisterHit(state, HIT_QUESTS_COMPLETE_ALL, x + w - 210.0f, y + 8.0f, 210.0f, 36.0f);

    float curY = y + 74.0f;

    if (state.activeProgress.running) {
        FillRoundRect(x, curY, w, 108.0f, 14.0f, cardBg);
        DrawRoundRect(x, curY, w, 108.0f, 14.0f, accent, 1.5f);
        DrawTextStr(state.activeProgress.questName, m_fmtCardTitle, x + 20.0f, curY + 16.0f, w - 140.0f, 24.0f, text);

        FillRoundRect(x + w - 96.0f, curY + 14.0f, 76.0f, 30.0f, 15.0f, 0x422F30);
        DrawTextStr(L.stopQuest, m_fmtSmall, x + w - 76.0f, curY + 21.0f, 56.0f, 18.0f, 0xFFB4AB);
        RegisterHit(state, HIT_QUESTS_STOP, x + w - 96.0f, curY + 14.0f, 76.0f, 30.0f);

        FillRoundRect(x + 20.0f, curY + 54.0f, w - 40.0f, 8.0f, 4.0f, 0x323436);
        float fillW = (w - 40.0f) * (float)(state.activeProgress.percent / 100.0);
        if (fillW < 12.0f) fillW = 12.0f;
        FillRoundRect(x + 20.0f, curY + 54.0f, fillW, 8.0f, 4.0f, accent);

        DrawTextStr(state.activeProgress.statusText, m_fmtSmall, x + 20.0f, curY + 72.0f, w - 160.0f, 20.0f, muted);
        curY += 128.0f;
    }

    int count = (std::min)((int)state.quests.size(), state.visibleQuestCount);
    int cols = (w > 620.0f) ? 2 : 1;
    float cardW = (cols == 2) ? ((w - 20.0f) * 0.5f) : w;
    float cardH = 186.0f;

    for (int i = 0; i < count; i++) {
        const auto& q = state.quests[i];
        int col = i % cols;
        int row = i / cols;
        float cx = x + col * (cardW + 20.0f);
        float cy = curY + row * (cardH + 18.0f);

        FillRoundRect(cx, cy, cardW, cardH, 14.0f, cardBg);
        FillRoundRect(cx, cy, cardW, 56.0f, 14.0f, q.primaryColor);
        DrawRoundRect(cx, cy, cardW, cardH, 14.0f, border);

        DrawTextStr(q.taskTypeW, m_fmtSmall, cx + 16.0f, cy + 66.0f, 180.0f, 18.0f, muted);
        if (q.orbQuantity > 0) {
            std::wstring orbText = L"+" + std::to_wstring(q.orbQuantity) + L" Orbs";
            FillRoundRect(cx + cardW - 88.0f, cy + 64.0f, 72.0f, 22.0f, 10.0f, 0x252C39);
            DrawTextStr(orbText, m_fmtSmall, cx + cardW - 78.0f, cy + 67.0f, 60.0f, 18.0f, 0x70B5FF);
        }

        DrawTextStr(q.name, m_fmtCardTitle, cx + 16.0f, cy + 90.0f, cardW - 32.0f, 24.0f, text);
        DrawTextStr(q.gameTitle, m_fmtSmall, cx + 16.0f, cy + 116.0f, cardW - 32.0f, 18.0f, muted);

        if (q.completed) {
            DrawTextStr(L"\x2713 " + std::wstring(L.questCompleted), m_fmtBodyBold, cx + cardW - 130.0f, cy + 146.0f, 114.0f, 24.0f, 0x8CD6A7);
        } else {
            FillRoundRect(cx + cardW - 146.0f, cy + 140.0f, 130.0f, 32.0f, 16.0f, accent);
            DrawTextStr(L.completeQuest, m_fmtSmall, cx + cardW - 130.0f, cy + 148.0f, 108.0f, 20.0f, onAccent);
            RegisterHit(state, HIT_QUEST_START_BASE + i, cx + cardW - 146.0f, cy + 140.0f, 130.0f, 32.0f);
        }
    }

    int totalRows = (count + cols - 1) / cols;
    float bottomY = curY + totalRows * (cardH + 18.0f);
    if (count < (int)state.quests.size()) {
        DrawRoundRect(x + (w - 160.0f) * 0.5f, bottomY + 8.0f, 160.0f, 36.0f, 18.0f, border);
        DrawTextStr(L.loadMoreQuests, m_fmtBodyBold, x + (w - 90.0f) * 0.5f, bottomY + 16.0f, 120.0f, 22.0f, accent);
        RegisterHit(state, HIT_QUESTS_LOAD_MORE, x + (w - 160.0f) * 0.5f, bottomY + 8.0f, 160.0f, 36.0f);
        bottomY += 60.0f;
    }
    state.maxScrollY = (std::max)(0, (int)(bottomY - 580.0f));
}

void UiRenderer::RenderSnowflakesTab(AppState& state, float x, float y, float w) {
    const auto& L = GetLocale(state.locale);
    UINT32 cardBg = state.darkMode ? 0x252628 : 0xFFFFFF;
    UINT32 border = state.darkMode ? 0x42454A : 0xDCE1E7;
    UINT32 text = state.darkMode ? 0xE8EAED : 0x202124;
    UINT32 muted = state.darkMode ? 0xADB2B9 : 0x5C626B;
    UINT32 accent = state.darkMode ? 0xA8CDFF : 0x0B57C3;
    UINT32 inputBg = state.darkMode ? 0x1E1F20 : 0xF8FAFD;

    DrawTextStr(L.tabSnowflakes, m_fmtTitle, x, y, 360.0f, 32.0f, text);
    DrawTextStr(L.snowflakesSubtitle, m_fmtBody, x, y + 34.0f, 620.0f, 22.0f, muted);

    float cy = y + 74.0f;
    FillRoundRect(x, cy, w, 132.0f, 16.0f, cardBg);
    DrawRoundRect(x, cy, w, 132.0f, 16.0f, border);
    DrawTextStr(L.snowflakeInputTitle, m_fmtCardTitle, x + 24.0f, cy + 18.0f, 300.0f, 24.0f, text);

    FillRoundRect(x + 24.0f, cy + 52.0f, w - 48.0f, 40.0f, 8.0f, inputBg);
    DrawRoundRect(x + 24.0f, cy + 52.0f, w - 48.0f, 40.0f, 8.0f,
                  state.focusedInput == FocusedInput::SnowflakeId ? accent : border);
    DrawTextStr(state.snowflakeInput.empty() ? L.snowflakePlaceholder : state.snowflakeInput,
                m_fmtMono, x + 36.0f, cy + 62.0f, w - 72.0f, 22.0f,
                state.snowflakeInput.empty() ? muted : text);
    RegisterHit(state, HIT_SNOWFLAKE_INPUT, x + 24.0f, cy + 52.0f, w - 48.0f, 40.0f);
    DrawTextStr(L.snowflakeInputHint, m_fmtSmall, x + 24.0f, cy + 100.0f, w - 48.0f, 18.0f, muted);

    if (state.snowflakeDecoded.valid) {
        float dy = cy + 150.0f;
        FillRoundRect(x, dy, w, 98.0f, 16.0f, cardBg);
        DrawRoundRect(x, dy, w, 98.0f, 16.0f, border);
        DrawTextStr(L.snowflakeCreatedAt, m_fmtSmall, x + 24.0f, dy + 16.0f, 200.0f, 18.0f, muted);
        DrawTextStr(state.snowflakeDecoded.formattedDate, m_fmtCardTitle, x + 24.0f, dy + 38.0f, w - 140.0f, 26.0f, text);
        DrawTextStr(state.snowflakeDecoded.relativeAge, m_fmtSmall, x + 24.0f, dy + 66.0f, 240.0f, 18.0f, muted);

        float gy = dy + 116.0f;
        FillRoundRect(x, gy, w, 190.0f, 16.0f, cardBg);
        DrawRoundRect(x, gy, w, 190.0f, 16.0f, border);
        DrawTextStr(L.snowflakeDetailsTitle, m_fmtCardTitle, x + 24.0f, gy + 18.0f, 300.0f, 24.0f, text);

        struct Row { const wchar_t* label; std::wstring val; };
        Row rows[6] = {
            {L.snowflakeRawId, state.snowflakeDecoded.rawId},
            {L.snowflakeUnixMs, std::to_wstring(state.snowflakeDecoded.timestampMs)},
            {L.snowflakeTimestampBits, state.snowflakeDecoded.timestampBits},
            {L.snowflakeWorkerId, state.snowflakeDecoded.workerId},
            {L.snowflakeProcessId, state.snowflakeDecoded.processId},
            {L.snowflakeIncrement, state.snowflakeDecoded.increment}
        };
        for (int i = 0; i < 6; i++) {
            float rx = x + 24.0f + (i % 2) * (w * 0.48f);
            float ry = gy + 54.0f + (i / 2) * 42.0f;
            DrawTextStr(rows[i].label, m_fmtSmall, rx, ry, 220.0f, 16.0f, muted);
            DrawTextStr(rows[i].val, m_fmtMono, rx, ry + 17.0f, 220.0f, 20.0f, text);
            RegisterHit(state, HIT_SNOWFLAKE_COPY_BASE + i, rx, ry, 220.0f, 38.0f);
        }
    }
}

void UiRenderer::RenderDiscordModal(AppState& state, float W, float H) {
    const auto& L = GetLocale(state.locale);
    FillRoundRect(0.0f, 0.0f, W, H, 0.0f, 0x000000, 0.65f);

    float mw = (std::min)(440.0f, W - 32.0f);
    float mh = 340.0f;
    float mx = (W - mw) * 0.5f;
    float my = (H - mh) * 0.5f;

    FillRoundRect(mx, my, mw, mh, 20.0f, 0x252628);
    DrawRoundRect(mx, my, mw, mh, 20.0f, 0x42454A);

    DrawTextStr(L.connectDiscord, m_fmtCardTitle, mx + 24.0f, my + 20.0f, 260.0f, 24.0f, 0xE8EAED);
    DrawTextStr(L"\x00D7", m_fmtTitle, mx + mw - 42.0f, my + 14.0f, 28.0f, 28.0f, 0xADB2B9);
    RegisterHit(state, HIT_MODAL_CLOSE, mx + mw - 48.0f, my + 12.0f, 36.0f, 36.0f);

    FillRoundRect(mx + 24.0f, my + 60.0f, mw - 48.0f, 36.0f, 10.0f, 0x323436);
    if (state.modalTab == ModalTab::Credentials) {
        FillRoundRect(mx + 28.0f, my + 64.0f, (mw - 56.0f) * 0.5f, 28.0f, 7.0f, 0x252628);
    } else {
        FillRoundRect(mx + 28.0f + (mw - 56.0f) * 0.5f, my + 64.0f, (mw - 56.0f) * 0.5f, 28.0f, 7.0f, 0x252628);
    }
    DrawTextStr(L.discordLoginTabCredentials, m_fmtSmall, mx + 48.0f, my + 70.0f, 140.0f, 20.0f, 0xE8EAED);
    DrawTextStr(L.discordLoginTabToken, m_fmtSmall, mx + 48.0f + (mw - 56.0f) * 0.5f, my + 70.0f, 140.0f, 20.0f, 0xE8EAED);
    RegisterHit(state, HIT_MODAL_TAB_CREDENTIALS, mx + 28.0f, my + 64.0f, (mw - 56.0f) * 0.5f, 28.0f);
    RegisterHit(state, HIT_MODAL_TAB_TOKEN, mx + 28.0f + (mw - 56.0f) * 0.5f, my + 64.0f, (mw - 56.0f) * 0.5f, 28.0f);

    if (state.modalTab == ModalTab::Token) {
        DrawTextStr(L.discordTokenInputLabel, m_fmtSmall, mx + 24.0f, my + 116.0f, mw - 48.0f, 18.0f, 0xE8EAED);
        FillRoundRect(mx + 24.0f, my + 140.0f, mw - 48.0f, 38.0f, 8.0f, 0x1E1F20);
        DrawRoundRect(mx + 24.0f, my + 140.0f, mw - 48.0f, 38.0f, 8.0f, 0xA8CDFF);
        DrawTextStr(state.modalToken.empty() ? L.discordTokenInputPlaceholder : state.modalToken,
                    m_fmtMono, mx + 36.0f, my + 150.0f, mw - 72.0f, 20.0f, 0xADB2B9);
        RegisterHit(state, HIT_MODAL_TOKEN_INPUT, mx + 24.0f, my + 140.0f, mw - 48.0f, 38.0f);
        DrawTextStr(L.discordTokenInputHint, m_fmtSmall, mx + 24.0f, my + 188.0f, mw - 48.0f, 32.0f, 0xADB2B9);
    } else {
        DrawTextStr(L.discordEmailLabel, m_fmtSmall, mx + 24.0f, my + 114.0f, mw - 48.0f, 18.0f, 0xE8EAED);
        FillRoundRect(mx + 24.0f, my + 136.0f, mw - 48.0f, 36.0f, 8.0f, 0x1E1F20);
        DrawRoundRect(mx + 24.0f, my + 136.0f, mw - 48.0f, 36.0f, 8.0f, 0x42454A);
        RegisterHit(state, HIT_MODAL_EMAIL_INPUT, mx + 24.0f, my + 136.0f, mw - 48.0f, 36.0f);

        DrawTextStr(L.discordPasswordLabel, m_fmtSmall, mx + 24.0f, my + 182.0f, mw - 48.0f, 18.0f, 0xE8EAED);
        FillRoundRect(mx + 24.0f, my + 204.0f, mw - 48.0f, 36.0f, 8.0f, 0x1E1F20);
        DrawRoundRect(mx + 24.0f, my + 204.0f, mw - 48.0f, 36.0f, 8.0f, 0x42454A);
        RegisterHit(state, HIT_MODAL_PASSWORD_INPUT, mx + 24.0f, my + 204.0f, mw - 48.0f, 36.0f);
    }

    FillRoundRect(mx + mw - 170.0f, my + mh - 56.0f, 146.0f, 36.0f, 18.0f, 0xA8CDFF);
    DrawTextStr(state.modalTab == ModalTab::Token ? L.discordSaveTokenBtn : L.connectDiscord,
                m_fmtSmall, mx + mw - 150.0f, my + mh - 46.0f, 120.0f, 20.0f, 0x10335E);
    RegisterHit(state, HIT_MODAL_SUBMIT, mx + mw - 170.0f, my + mh - 56.0f, 146.0f, 36.0f);
}
