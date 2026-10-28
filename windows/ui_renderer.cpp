#include "ui_renderer.h"
#include "image_cache.h"

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
    if (m_moonGeo) m_moonGeo->Release();
    if (m_roundStroke) m_roundStroke->Release();
    if (m_dwrite) m_dwrite->Release();
    if (m_factory) m_factory->Release();
}

bool UiRenderer::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    UINT dpi = GetDpiForWindow(hwnd);
    m_dpi = (dpi > 0) ? static_cast<float>(dpi) : 96.0f;

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_factory))) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&m_dwrite)))) return false;

    D2D1_STROKE_STYLE_PROPERTIES ssp = D2D1::StrokeStyleProperties(
        D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
        D2D1_LINE_JOIN_ROUND, 10.0f, D2D1_DASH_STYLE_SOLID, 0.0f);
    m_factory->CreateStrokeStyle(ssp, nullptr, 0, &m_roundStroke);

    // Exact SVG path for IconMoon: M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z (centered at 0,0, scaled to 18x18)
    if (SUCCEEDED(m_factory->CreatePathGeometry(&m_moonGeo))) {
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(m_moonGeo->Open(&sink))) {
            const float s = 18.0f / 24.0f;
            sink->BeginFigure(D2D1::Point2F((21.0f - 12.0f) * s, (12.79f - 12.0f) * s), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F((11.21f - 12.0f) * s, (3.0f - 12.0f) * s),
                D2D1::SizeF(9.0f * s, 9.0f * s), 0.0f,
                D2D1_SWEEP_DIRECTION_CLOCKWISE, D2D1_ARC_SIZE_LARGE));
            sink->AddArc(D2D1::ArcSegment(
                D2D1::Point2F((21.0f - 12.0f) * s, (12.79f - 12.0f) * s),
                D2D1::SizeF(7.0f * s, 7.0f * s), 0.0f,
                D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
        }
    }

    m_dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 24.0f, L"en-us", &m_fmtTitle);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Display", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 20.0f, L"en-us", &m_fmtWordmark);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 17.0f, L"en-us", &m_fmtCardTitle);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", &m_fmtBody);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.5f, L"en-us", &m_fmtBodyBold);
    m_dwrite->CreateTextFormat(L"Segoe UI Variable Text", nullptr, DWRITE_FONT_WEIGHT_MEDIUM, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 11.5f, L"en-us", &m_fmtSmall);
    m_dwrite->CreateTextFormat(L"Cascadia Mono", nullptr, DWRITE_FONT_WEIGHT_REGULAR, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 13.0f, L"en-us", &m_fmtMono);

    ImageCache::Instance().Initialize(hwnd);

    return EnsureTarget();
}

bool UiRenderer::EnsureTarget() {
    if (m_rt) return true;
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    D2D1_SIZE_U sz = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    D2D1_RENDER_TARGET_PROPERTIES rtProps = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE),
        m_dpi, m_dpi
    );
    if (FAILED(m_factory->CreateHwndRenderTarget(rtProps, D2D1::HwndRenderTargetProperties(m_hwnd, sz), &m_rt))) {
        return false;
    }
    m_rt->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    m_rt->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &m_brush);
    return true;
}

void UiRenderer::SetDpi(UINT dpi) {
    m_dpi = (dpi > 0) ? static_cast<float>(dpi) : 96.0f;
    if (m_rt) {
        m_rt->SetDpi(m_dpi, m_dpi);
    }
}

void UiRenderer::DiscardResources() {
    ImageCache::Instance().DiscardGpuResources();
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

void UiRenderer::DrawThemeIcon(float cx, float cy, bool isDark, UINT32 rgb, float alpha) {
    if (!m_rt || !m_brush) return;
    SetColor(rgb, alpha);
    const float s = 18.0f / 24.0f;
    const float strokeW = 1.65f;
    if (isDark) {
        // IconSun from frontend/src/components/icons/index.tsx: circle r=4.5 + 8 round-capped rays
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), 4.6f * s, 4.6f * s);
        m_rt->DrawEllipse(el, m_brush, strokeW, m_roundStroke);
        const float r1 = 7.8f * s;
        const float r2 = 10.2f * s;
        const float diag = 0.70710678f;
        const float dirs[8][2] = {
            {0.0f, -1.0f}, {0.0f, 1.0f}, {-1.0f, 0.0f}, {1.0f, 0.0f},
            {-diag, -diag}, {diag, diag}, {-diag, diag}, {diag, -diag}
        };
        for (int i = 0; i < 8; ++i) {
            m_rt->DrawLine(
                D2D1::Point2F(cx + dirs[i][0] * r1, cy + dirs[i][1] * r1),
                D2D1::Point2F(cx + dirs[i][0] * r2, cy + dirs[i][1] * r2),
                m_brush, strokeW, m_roundStroke);
        }
    } else if (m_moonGeo) {
        // IconMoon from frontend/src/components/icons/index.tsx
        D2D1_MATRIX_3X2_F oldTransform;
        m_rt->GetTransform(&oldTransform);
        m_rt->SetTransform(D2D1::Matrix3x2F::Translation(cx, cy) * oldTransform);
        m_rt->DrawGeometry(m_moonGeo, m_brush, strokeW, m_roundStroke);
        m_rt->SetTransform(oldTransform);
    }
}

void UiRenderer::DrawLogoutIcon(float cx, float cy, UINT32 rgb, float alpha) {
    if (!m_rt || !m_brush) return;
    SetColor(rgb, alpha);
    const float s = 15.0f / 24.0f;
    const float strokeW = 1.65f;
    // IconLogOut from frontend/src/components/icons/index.tsx:
    // Door frame on left: (9,21) -> (5,21) -> (3,19) -> (3,5) -> (5,3) -> (9,3)
    auto pt = [&](float ux, float uy) {
        return D2D1::Point2F(cx + (ux - 12.0f) * s, cy + (uy - 12.0f) * s);
    };
    m_rt->DrawLine(pt(9.0f, 20.0f), pt(4.0f, 20.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(4.0f, 20.0f), pt(4.0f, 4.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(4.0f, 4.0f), pt(9.0f, 4.0f), m_brush, strokeW, m_roundStroke);
    // Arrow shaft & chevron: (9,12) -> (20,12), (16,7) -> (21,12) -> (16,17)
    m_rt->DrawLine(pt(9.0f, 12.0f), pt(20.5f, 12.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(16.0f, 7.5f), pt(20.5f, 12.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(20.5f, 12.0f), pt(16.0f, 16.5f), m_brush, strokeW, m_roundStroke);
}

void UiRenderer::DrawIconUser(float cx, float cy, float size, UINT32 rgb, float alpha) {
    if (!m_rt || !m_brush) return;
    SetColor(rgb, alpha);
    const float s = size / 24.0f;
    const float strokeW = 1.75f;
    auto pt = [&](float ux, float uy) {
        return D2D1::Point2F(cx + (ux - 12.0f) * s, cy + (uy - 12.0f) * s);
    };
    D2D1_ELLIPSE head = D2D1::Ellipse(pt(12.0f, 7.0f), 4.0f * s, 4.0f * s);
    m_rt->DrawEllipse(head, m_brush, strokeW, m_roundStroke);

    ID2D1PathGeometry* geo = nullptr;
    if (SUCCEEDED(m_factory->CreatePathGeometry(&geo))) {
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink))) {
            sink->BeginFigure(pt(19.0f, 21.0f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(pt(19.0f, 19.0f));
            sink->AddArc(D2D1::ArcSegment(pt(15.0f, 15.0f), D2D1::SizeF(4.0f * s, 4.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(pt(9.0f, 15.0f));
            sink->AddArc(D2D1::ArcSegment(pt(5.0f, 19.0f), D2D1::SizeF(4.0f * s, 4.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(pt(5.0f, 21.0f));
            sink->EndFigure(D2D1_FIGURE_END_OPEN);
            sink->Close();
            sink->Release();
            m_rt->DrawGeometry(geo, m_brush, strokeW, m_roundStroke);
        }
        geo->Release();
    }
}

void UiRenderer::DrawIconSparkles(float cx, float cy, float size, UINT32 rgb, float alpha) {
    if (!m_rt || !m_brush) return;
    SetColor(rgb, alpha);
    const float s = size / 24.0f;
    const float strokeW = 1.75f;
    auto pt = [&](float ux, float uy) {
        return D2D1::Point2F(cx + (ux - 12.0f) * s, cy + (uy - 12.0f) * s);
    };

    ID2D1PathGeometry* geo = nullptr;
    if (SUCCEEDED(m_factory->CreatePathGeometry(&geo))) {
        ID2D1GeometrySink* sink = nullptr;
        if (SUCCEEDED(geo->Open(&sink))) {
            sink->BeginFigure(pt(12.0f, 3.0f), D2D1_FIGURE_BEGIN_HOLLOW);
            sink->AddLine(pt(10.1f, 8.8f));
            sink->AddArc(D2D1::ArcSegment(pt(8.8f, 10.1f), D2D1::SizeF(2.0f * s, 2.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(pt(3.0f, 12.0f));
            sink->AddLine(pt(8.8f, 13.9f));
            sink->AddArc(D2D1::ArcSegment(pt(10.1f, 15.2f), D2D1::SizeF(2.0f * s, 2.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(pt(12.0f, 21.0f));
            sink->AddLine(pt(13.9f, 15.2f));
            sink->AddArc(D2D1::ArcSegment(pt(15.2f, 13.9f), D2D1::SizeF(2.0f * s, 2.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->AddLine(pt(21.0f, 12.0f));
            sink->AddLine(pt(15.2f, 10.1f));
            sink->AddArc(D2D1::ArcSegment(pt(13.9f, 8.8f), D2D1::SizeF(2.0f * s, 2.0f * s), 0.0f, D2D1_SWEEP_DIRECTION_COUNTER_CLOCKWISE, D2D1_ARC_SIZE_SMALL));
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            sink->Close();
            sink->Release();
            m_rt->DrawGeometry(geo, m_brush, strokeW, m_roundStroke);
        }
        geo->Release();
    }
}

void UiRenderer::DrawIconSnowflake(float cx, float cy, float size, UINT32 rgb, float alpha) {
    if (!m_rt || !m_brush) return;
    SetColor(rgb, alpha);
    const float s = size / 24.0f;
    const float strokeW = 1.75f;
    auto pt = [&](float ux, float uy) {
        return D2D1::Point2F(cx + (ux - 12.0f) * s, cy + (uy - 12.0f) * s);
    };

    m_rt->DrawLine(pt(12.0f, 2.0f), pt(12.0f, 22.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(3.34f, 7.0f), pt(20.66f, 17.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(3.34f, 17.0f), pt(20.66f, 7.0f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(9.0f, 5.0f), pt(12.0f, 2.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(12.0f, 2.0f), pt(15.0f, 5.0f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(9.0f, 19.0f), pt(12.0f, 22.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(12.0f, 22.0f), pt(15.0f, 19.0f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(5.94f, 5.5f), pt(3.34f, 7.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(3.34f, 7.0f), pt(3.44f, 10.0f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(18.06f, 18.5f), pt(20.66f, 17.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(20.66f, 17.0f), pt(20.56f, 14.0f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(3.44f, 14.0f), pt(3.34f, 17.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(3.34f, 17.0f), pt(5.94f, 18.5f), m_brush, strokeW, m_roundStroke);

    m_rt->DrawLine(pt(20.56f, 10.0f), pt(20.66f, 7.0f), m_brush, strokeW, m_roundStroke);
    m_rt->DrawLine(pt(20.66f, 7.0f), pt(18.06f, 5.5f), m_brush, strokeW, m_roundStroke);
}

void UiRenderer::RegisterHit(AppState& state, int id, float x, float y, float w, float h) {
    float scale = m_dpi / 96.0f;
    RECT rc;
    rc.left = static_cast<LONG>(x * scale);
    rc.top = static_cast<LONG>(y * scale);
    rc.right = static_cast<LONG>((x + w) * scale);
    rc.bottom = static_cast<LONG>((y + h) * scale);
    state.clickTargets.push_back({id, rc});
}

void UiRenderer::Render(AppState& state) {
    if (!EnsureTarget()) return;

    std::lock_guard<std::mutex> lk(state.mu);
    state.clickTargets.clear();
    state.dpiScale = m_dpi / 96.0f;

    D2D1_SIZE_F sz = m_rt->GetSize();
    float W = sz.width;
    float H = sz.height;

    UINT32 fallbackBg = state.darkMode ? 0x1E1F20 : 0xF3F6FC;

    m_rt->BeginDraw();
    m_rt->Clear(D2D1::ColorF(
        ((fallbackBg >> 16) & 0xFF) / 255.0f,
        ((fallbackBg >> 8) & 0xFF) / 255.0f,
        (fallbackBg & 0xFF) / 255.0f,
        1.0f
    ));

    if (!state.isAuthenticated) {
        RenderLoginScreen(state, W, H);
    } else {
        float contentX = (W > 700.0f) ? 252.0f : 20.0f;
        float contentW = (W > 700.0f) ? (W - 272.0f) : (W - 40.0f);
        if (contentW > 852.0f) {
            contentX += (contentW - 852.0f) * 0.5f;
            contentW = 852.0f;
        }
        float contentY = 92.0f - static_cast<float>(state.scrollY);

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
