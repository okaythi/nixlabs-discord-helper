#pragma once
#include "app_state.h"
#include <d2d1.h>
#include <dwrite.h>

enum TargetId {
     HIT_NONE = 0,
     HIT_THEME_TOGGLE = 1,
     HIT_LOGOUT = 2,
     HIT_LOCALE_CYCLE = 3,
     HIT_TAB_ACCOUNT = 10,
     HIT_TAB_QUESTS = 11,
     HIT_TAB_SNOWFLAKES = 12,
     HIT_ECOSYSTEM_LINK = 13,
     HIT_LOGIN_SHOW_FORM = 20,
     HIT_LOGIN_CREATE_ACCOUNT = 21,
     HIT_LOGIN_IDENTIFIER_INPUT = 22,
     HIT_LOGIN_PASSWORD_INPUT = 23,
     HIT_LOGIN_SUBMIT = 24,
     HIT_LOGIN_BACK = 25,
     HIT_DISCORD_CONNECT_BTN = 30,
     HIT_DISCORD_REFRESH_BTN = 31,
     HIT_DISCORD_DISCONNECT_BTN = 32,
     HIT_MODAL_CLOSE = 40,
     HIT_MODAL_TAB_CREDENTIALS = 41,
     HIT_MODAL_TAB_TOKEN = 42,
     HIT_MODAL_EMAIL_INPUT = 43,
     HIT_MODAL_PASSWORD_INPUT = 44,
     HIT_MODAL_TOKEN_INPUT = 45,
     HIT_MODAL_SUBMIT = 46,
     HIT_MODAL_MYACCOUNT_LINK = 47,
     HIT_QUESTS_REFRESH = 50,
     HIT_QUESTS_COMPLETE_ALL = 51,
     HIT_QUESTS_STOP = 52,
     HIT_QUESTS_LOAD_MORE = 53,
     HIT_SNOWFLAKE_INPUT = 60,
     HIT_SNOWFLAKE_COPY_ISO = 61,
     HIT_SNOWFLAKE_COPY_BASE = 70, // 70..75
     HIT_LOCALE_OPTION_BASE = 80,  // 80..85
     HIT_QUEST_START_BASE = 1000   // 1000 + index
};

class UiRenderer {
public:
    UiRenderer();
    ~UiRenderer();

    bool Initialize(HWND hwnd);
    void SetDpi(UINT dpi);
    void Resize(UINT width, UINT height);
    void Render(AppState& state);
    void DiscardResources();

private:
    HWND m_hwnd = nullptr;
    float m_dpi = 96.0f;
    ID2D1Factory* m_factory = nullptr;
    IDWriteFactory* m_dwrite = nullptr;
    ID2D1HwndRenderTarget* m_rt = nullptr;
    ID2D1SolidColorBrush* m_brush = nullptr;

    IDWriteTextFormat* m_fmtTitle = nullptr;
    IDWriteTextFormat* m_fmtWordmark = nullptr;
    IDWriteTextFormat* m_fmtCardTitle = nullptr;
    IDWriteTextFormat* m_fmtBody = nullptr;
    IDWriteTextFormat* m_fmtBodyBold = nullptr;
    IDWriteTextFormat* m_fmtSmall = nullptr;
    IDWriteTextFormat* m_fmtMono = nullptr;
    ID2D1StrokeStyle* m_roundStroke = nullptr;
    ID2D1PathGeometry* m_moonGeo = nullptr;

    bool EnsureTarget();
    void SetColor(UINT32 rgb, float alpha = 1.0f);
    void FillRoundRect(float x, float y, float w, float h, float r, UINT32 rgb, float alpha = 1.0f);
    void DrawRoundRect(float x, float y, float w, float h, float r, UINT32 rgb, float stroke = 1.0f, float alpha = 1.0f);
    void DrawTextStr(const std::wstring& text, IDWriteTextFormat* fmt, float x, float y, float w, float h, UINT32 rgb, float alpha = 1.0f);
    void DrawThemeIcon(float cx, float cy, bool isDark, UINT32 rgb, float alpha = 1.0f);
    void DrawLogoutIcon(float cx, float cy, UINT32 rgb, float alpha = 1.0f);
    void DrawIconUser(float cx, float cy, float size, UINT32 rgb, float alpha = 1.0f);
    void DrawIconSparkles(float cx, float cy, float size, UINT32 rgb, float alpha = 1.0f);
    void DrawIconSnowflake(float cx, float cy, float size, UINT32 rgb, float alpha = 1.0f);
    void RegisterHit(AppState& state, int id, float x, float y, float w, float h);

    void RenderLoginScreen(AppState& state, float W, float H);
    void RenderHeaderAndSidebar(AppState& state, float W, float H);
    void RenderAccountTab(AppState& state, float contentX, float contentY, float contentW);
    void RenderQuestsTab(AppState& state, float contentX, float contentY, float contentW);
    void RenderSnowflakesTab(AppState& state, float contentX, float contentY, float contentW);
    void RenderDiscordModal(AppState& state, float W, float H);
};
