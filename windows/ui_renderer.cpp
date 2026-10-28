#include "ui_renderer.h"

UiRenderer::UiRenderer() {}

UiRenderer::~UiRenderer() {
    DiscardResources();
    if (m_fmtTitle) m_fmtTitle->Release();
    if (m_fmtWordmark) m_fmtWordmark->Release();
    if (m_fmtCardTitle) m_fmtCardTitle->Release();
    if (m_fmtBody) m_fmtBody->Release();
    if (m_fmtBodyBold) m_fmtBodyBold->Release();
    if (m_fmtSmall) m_fmtSmall->Release();
    if (m_fmtMono) m_fmtMono->Release();
    if (m_dwrite) m_dwrite->Release();
    if (m_factory) m_factory->Release();
}

bool UiRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwrite)))) return false;

    m_dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-us", &m_fmtTitle);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &m_fmtWordmark);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-us", &m_fmtCardTitle);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", &m_fmtBody);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", &m_fmtBodyBold);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &m_fmtSmall);
    m_dwrite->CreateTextFormat(L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &m_fmtMono);

    return EnsureTarget();
}

bool UiRenderer::EnsureTarget() {
    if (m_rt) return true;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties();
    rtProps.dpiX = 96.0f;
    rtProps.dpiY = 96.0f;
    if (FAILED(m_factory->CreateHwndRenderTarget(rtProps, D2D1::HwndRenderTargetProperties(m_hwnd, sz), &m_rt))) {
        return false;
    }
    m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_brush);
    return true;
}

void UiRenderer::DiscardResources() {
    if (m_brush) { m_brush->Release(); m_brush = nullptr; }
    if (m_rt) { m_rt->Release(); m_rt = nullptr; }
}

void UiRenderer::Resize(UINT width, UINT height) {
    if (m_rt) {
        m_rt->Resize(D2D1::SizeU(width, height));
    }
}

void UiRenderer::SetColor(UINT32 rgb, float alpha) {
    if (!m_brush) return;
    float r = ((rgb >> 16) & 0xFF) / 255.0f;
    float g = ((rgb >> 8) & 0xFF) / 255.0f;
    float b = (rgb & 0xFF) / 255.0f;
    m_brush->SetColor(D2D1::ColorF(r, g, b, alpha));
}

void UiRenderer::FillRoundRect(float x, float y, float w, float h, float r, UINT32 rgb, float alpha) {
    SetColor(rgb, alpha);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
    m_rt->FillRoundedRectangle(rr, m_brush);
}

void UiRenderer::DrawRoundRect(float x, float y, float w, float h, float r, UINT32 rgb, float stroke, float alpha) {
    SetColor(rgb, alpha);
    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(D2D1::RectF(x, y, x + w, y + h), r, r);
    m_rt->DrawRoundedRectangle(rr, m_brush, stroke);
}

void UiRenderer::DrawTextStr(const std::wstring& text, IDWriteTextFormat* fmt, float x, float y, float w, float h, UINT32 rgb, float alpha) {
    if (!fmt || text.empty()) return;
    SetColor(rgb, alpha);
    m_rt->DrawText(text.c_str(), (UINT32)text.size(), fmt, D2D1::RectF(x, y, x + w, y + h), m_brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

void UiRenderer::RegisterHit(AppState& state, int id, float x, float y, float w, float h) {
    RECT rc;
    rc.left = (LONG)x;
    rc.top = (LONG)y;
    rc.right = (LONG)(x + w);
    rc.bottom = (LONG)(y + h);
    state.clickTargets.push_back({id, rc});
}

void UiRenderer::Render(AppState& state) {
    if (!EnsureTarget()) return;

    std::lock_guard<std::mutex> lk(state.mu);
    state.clickTargets.clear();

    D2D1_SIZE_F sz = m_rt->GetSize();
    float W = sz.width;
    float H = sz.height;

    UINT32 bg = state.darkMode ? 0x1E1F20 : 0xF8FAFD;

    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(bg));

    if (!state.isAuthenticated) {
        RenderLoginScreen(state, W, H);
    } else {
        float contentX = (W > 760.0f) ? 260.0f : 24.0f;
        float contentW = (W > 760.0f) ? (W - 284.0f) : (W - 48.0f);
        if (contentW > 852.0f) {
            contentX += (contentW - 852.0f) * 0.5f;
            contentW = 852.0f;
        }
        float contentY = 96.0f - (float)state.scrollY;

        if (state.activeTab == NavTab::Account) {
            RenderAccountTab(state, contentX, contentY, contentW);
        } else if (state.activeTab == NavTab::Quests) {
            RenderQuestsTab(state, contentX, contentY, contentW);
        } else {
            RenderSnowflakesTab(state, contentX, contentY, contentW);
        }

        RenderHeaderAndSidebar(state, W, H);

        if (state.isModalOpen) {
            RenderDiscordModal(state, W, H);
        }
    }

    HRESULT hr = m_rt->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardResources();
    }
}
