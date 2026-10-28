#include "image_cache.h"
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <algorithm>
#include <thread>
#include <cmath>

#define WM_APP_STATE_UPDATED (WM_USER + 101)

#ifndef GUID_WICPixelFormat32bppPBGRA
static const GUID GUID_WICPixelFormat32bppPBGRA =
    { 0x6fddc324, 0x4e03, 0x4bfe, { 0xb1, 0x85, 0x3d, 0x77, 0x76, 0x8d, 0xc9, 0x10 } };
#endif
#ifndef CLSID_WICImagingFactory
static const CLSID CLSID_WICImagingFactory =
    { 0xcacaf262, 0xb42d, 0x498e, { 0x82, 0x42, 0x98, 0x6a, 0x89, 0x51, 0x5a, 0x36 } };
#endif

ImageCache& ImageCache::Instance() {
    static ImageCache s_instance;
    return s_instance;
}

ImageCache::ImageCache() = default;

ImageCache::~ImageCache() {
    Shutdown();
}

void ImageCache::Initialize(HWND hwnd) {
    m_hwnd = hwnd;
    EnsureWicFactory();
}

void ImageCache::Shutdown() {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto& pair : m_entries) {
        if (pair.second.bitmap) {
            pair.second.bitmap->Release();
            pair.second.bitmap = nullptr;
        }
        if (pair.second.converter) {
            pair.second.converter->Release();
            pair.second.converter = nullptr;
        }
    }
    m_entries.clear();
    if (m_wicFactory) {
        m_wicFactory->Release();
        m_wicFactory = nullptr;
    }
}

void ImageCache::DiscardGpuResources() {
    std::lock_guard<std::mutex> lk(m_mu);
    for (auto& pair : m_entries) {
        if (pair.second.bitmap) {
            pair.second.bitmap->Release();
            pair.second.bitmap = nullptr;
        }
    }
}

void ImageCache::EnsureWicFactory() {
    if (m_wicFactory) return;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&m_wicFactory)
    );
    if (FAILED(hr)) {
        // Fallback for Windows Imaging Component 2
        static const CLSID CLSID_WIC2 = { 0x317d06e8, 0x5f24, 0x433d, { 0xbd, 0xf7, 0x79, 0xce, 0x68, 0xd8, 0xab, 0xc2 } };
        CoCreateInstance(CLSID_WIC2, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wicFactory));
    }
}

std::wstring ImageCache::GetDiskCachePath(const std::wstring& url) {
    static std::wstring s_cacheDir;
    if (s_cacheDir.empty()) {
        wchar_t appData[MAX_PATH] = {0};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, appData))) {
            s_cacheDir = std::wstring(appData) + L"\\nixlabs-discord-helper\\cache\\images";
        } else {
            s_cacheDir = L".\\cache\\images";
        }
        CreateDirectoryW((std::wstring(appData) + L"\\nixlabs-discord-helper").c_str(), nullptr);
        CreateDirectoryW((std::wstring(appData) + L"\\nixlabs-discord-helper\\cache").c_str(), nullptr);
        CreateDirectoryW(s_cacheDir.c_str(), nullptr);
    }

    uint64_t hash = 14695981039346656037ULL;
    for (wchar_t c : url) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ULL;
    }
    wchar_t buf[64];
    swprintf(buf, 64, L"\\img_%016llx.bin", static_cast<unsigned long long>(hash));
    return s_cacheDir + buf;
}

std::vector<BYTE> ImageCache::ReadDiskFile(const std::wstring& path) {
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return {};

    LARGE_INTEGER sz;
    if (!GetFileSizeEx(hFile, &sz) || sz.QuadPart <= 0 || sz.QuadPart > (16 * 1024 * 1024)) {
        CloseHandle(hFile);
        return {};
    }

    std::vector<BYTE> buf(static_cast<size_t>(sz.QuadPart));
    DWORD read = 0;
    ReadFile(hFile, buf.data(), static_cast<DWORD>(buf.size()), &read, nullptr);
    CloseHandle(hFile);
    if (read != buf.size()) buf.resize(read);
    return buf;
}

void ImageCache::WriteDiskFile(const std::wstring& path, const std::vector<BYTE>& data) {
    if (data.empty()) return;
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(hFile, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    CloseHandle(hFile);
}

std::vector<BYTE> ImageCache::DownloadUrl(const std::wstring& url) {
    URL_COMPONENTSW comp = { sizeof(comp) };
    comp.dwHostNameLength = static_cast<DWORD>(-1);
    comp.dwUrlPathLength = static_cast<DWORD>(-1);
    comp.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &comp)) {
        return {};
    }

    std::wstring host(comp.lpszHostName, comp.dwHostNameLength);
    std::wstring path(comp.lpszUrlPath, comp.dwUrlPathLength);
    if (comp.dwExtraInfoLength > 0 && comp.lpszExtraInfo) {
        path.append(comp.lpszExtraInfo, comp.dwExtraInfoLength);
    }
    INTERNET_PORT port = comp.nPort;
    DWORD flags = (comp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hSession = WinHttpOpen(L"NixlabsDiscordHelper/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return {};

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect) {
        WinHttpCloseHandle(hSession);
        return {};
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return {};
    }

    std::vector<BYTE> result;
    if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(hRequest, nullptr)) {
        DWORD dwSize = 0;
        while (WinHttpQueryDataAvailable(hRequest, &dwSize) && dwSize > 0) {
            size_t curSize = result.size();
            result.resize(curSize + dwSize);
            DWORD dwRead = 0;
            if (WinHttpReadData(hRequest, result.data() + curSize, dwSize, &dwRead) && dwRead > 0) {
                result.resize(curSize + dwRead);
            } else {
                result.resize(curSize);
                break;
            }
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return result;
}

IWICFormatConverter* ImageCache::DecodeImageMemory(const std::vector<BYTE>& data, UINT32& outW, UINT32& outH) {
    EnsureWicFactory();
    if (!m_wicFactory || data.empty()) return nullptr;

    IWICStream* pStream = nullptr;
    if (FAILED(m_wicFactory->CreateStream(&pStream))) return nullptr;
    if (FAILED(pStream->InitializeFromMemory(const_cast<BYTE*>(data.data()), static_cast<DWORD>(data.size())))) {
        pStream->Release();
        return nullptr;
    }

    IWICBitmapDecoder* pDecoder = nullptr;
    if (FAILED(m_wicFactory->CreateDecoderFromStream(pStream, nullptr, WICDecodeMetadataCacheOnDemand, &pDecoder))) {
        pStream->Release();
        return nullptr;
    }

    IWICBitmapFrameDecode* pFrame = nullptr;
    if (FAILED(pDecoder->GetFrame(0, &pFrame))) {
        pDecoder->Release();
        pStream->Release();
        return nullptr;
    }

    pFrame->GetSize(&outW, &outH);

    IWICFormatConverter* pConverter = nullptr;
    if (SUCCEEDED(m_wicFactory->CreateFormatConverter(&pConverter))) {
        HRESULT hr = pConverter->Initialize(
            pFrame,
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeMedianCut
        );
        if (FAILED(hr)) {
            pConverter->Release();
            pConverter = nullptr;
        }
    }

    pFrame->Release();
    pDecoder->Release();
    pStream->Release();
    return pConverter;
}

void ImageCache::TriggerFetch(const std::wstring& url) {
    std::thread([this, url]() {
        HRESULT hrCom = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

        std::wstring diskPath = GetDiskCachePath(url);
        std::vector<BYTE> data = ReadDiskFile(diskPath);
        if (data.empty()) {
            data = DownloadUrl(url);
            if (!data.empty()) {
                WriteDiskFile(diskPath, data);
            }
        }

        UINT32 w = 0, h = 0;
        IWICFormatConverter* converter = nullptr;
        if (!data.empty()) {
            converter = DecodeImageMemory(data, w, h);
        }

        {
            std::lock_guard<std::mutex> lk(m_mu);
            auto it = m_entries.find(url);
            if (it != m_entries.end()) {
                it->second.isLoading = false;
                if (converter) {
                    it->second.converter = converter;
                    it->second.width = w;
                    it->second.height = h;
                    it->second.isFailed = false;
                } else {
                    it->second.isFailed = true;
                }
            }
        }

        if (SUCCEEDED(hrCom)) {
            CoUninitialize();
        }

        if (m_hwnd && converter) {
            PostMessageW(m_hwnd, WM_APP_STATE_UPDATED, 0, 0);
        }
    }).detach();
}

ID2D1Bitmap* ImageCache::GetBitmap(ID2D1RenderTarget* rt, const std::wstring& url) {
    if (url.empty() || !rt) return nullptr;

    std::lock_guard<std::mutex> lk(m_mu);
    auto it = m_entries.find(url);
    if (it != m_entries.end()) {
        if (it->second.bitmap) {
            return it->second.bitmap;
        }
        if (it->second.converter) {
            HRESULT hr = rt->CreateBitmapFromWicBitmap(it->second.converter, nullptr, &it->second.bitmap);
            it->second.converter->Release();
            it->second.converter = nullptr;
            if (SUCCEEDED(hr)) {
                return it->second.bitmap;
            } else {
                it->second.isFailed = true;
                return nullptr;
            }
        }
        if (it->second.isLoading || it->second.isFailed) {
            return nullptr;
        }
    }

    // New entry: mark as loading and trigger async download/decode
    ImageCacheEntry& entry = m_entries[url];
    entry.url = url;
    entry.isLoading = true;
    entry.isFailed = false;
    TriggerFetch(url);
    return nullptr;
}

bool ImageCache::DrawCircleImage(ID2D1RenderTarget* rt, const std::wstring& url, float cx, float cy, float radius, float alpha) {
    ID2D1Bitmap* bmp = GetBitmap(rt, url);
    if (!bmp) return false;

    D2D1_SIZE_F sz = bmp->GetSize();
    if (sz.width <= 0.0f || sz.height <= 0.0f) return false;

    ID2D1BitmapBrush* brush = nullptr;
    D2D1_BITMAP_BRUSH_PROPERTIES bp = D2D1::BitmapBrushProperties(
        D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    D2D1_BRUSH_PROPERTIES brp = D2D1::BrushProperties(alpha);

    if (FAILED(rt->CreateBitmapBrush(bmp, bp, brp, &brush))) {
        return false;
    }

    float diam = radius * 2.0f;
    float scale = (std::max)(diam / sz.width, diam / sz.height);
    float scaledW = sz.width * scale;
    float scaledH = sz.height * scale;
    float ox = (cx - radius) - (scaledW - diam) * 0.5f;
    float oy = (cy - radius) - (scaledH - diam) * 0.5f;

    D2D1_MATRIX_3X2_F mat = D2D1::Matrix3x2F::Scale(scale, scale) *
                            D2D1::Matrix3x2F::Translation(ox, oy);
    brush->SetTransform(mat);

    D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F(cx, cy), radius, radius);
    rt->FillEllipse(el, brush);
    brush->Release();
    return true;
}

bool ImageCache::DrawRoundedImage(ID2D1RenderTarget* rt, const std::wstring& url, const D2D1_RECT_F& destRect, float rx, float ry, float alpha) {
    ID2D1Bitmap* bmp = GetBitmap(rt, url);
    if (!bmp) return false;

    D2D1_SIZE_F sz = bmp->GetSize();
    if (sz.width <= 0.0f || sz.height <= 0.0f) return false;

    float w = destRect.right - destRect.left;
    float h = destRect.bottom - destRect.top;
    if (w <= 0.0f || h <= 0.0f) return false;

    ID2D1BitmapBrush* brush = nullptr;
    D2D1_BITMAP_BRUSH_PROPERTIES bp = D2D1::BitmapBrushProperties(
        D2D1_EXTEND_MODE_CLAMP, D2D1_EXTEND_MODE_CLAMP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
    D2D1_BRUSH_PROPERTIES brp = D2D1::BrushProperties(alpha);

    if (FAILED(rt->CreateBitmapBrush(bmp, bp, brp, &brush))) {
        return false;
    }

    float scale = (std::max)(w / sz.width, h / sz.height);
    float scaledW = sz.width * scale;
    float scaledH = sz.height * scale;
    float ox = destRect.left - (scaledW - w) * 0.5f;
    float oy = destRect.top - (scaledH - h) * 0.5f;

    D2D1_MATRIX_3X2_F mat = D2D1::Matrix3x2F::Scale(scale, scale) *
                            D2D1::Matrix3x2F::Translation(ox, oy);
    brush->SetTransform(mat);

    D2D1_ROUNDED_RECT rr = D2D1::RoundedRect(destRect, rx, ry);
    rt->FillRoundedRectangle(rr, brush);
    brush->Release();
    return true;
}
