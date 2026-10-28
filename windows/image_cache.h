#pragma once
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <string>
#include <unordered_map>
#include <mutex>
#include <vector>

struct ImageCacheEntry {
    std::wstring url;
    IWICFormatConverter* converter = nullptr;
    ID2D1Bitmap* bitmap = nullptr;
    bool isLoading = false;
    bool isFailed = false;
    UINT32 width = 0;
    UINT32 height = 0;
};

class ImageCache {
public:
    static ImageCache& Instance();

    void Initialize(HWND hwnd);
    void Shutdown();
    void DiscardGpuResources();

    // Renders circular image (e.g. Discord avatars). Returns true if drawn, false if not ready.
    bool DrawCircleImage(ID2D1RenderTarget* rt, const std::wstring& url, float cx, float cy, float radius, float alpha = 1.0f);

    // Renders rounded rectangle image (e.g. Discord banners, Quest cards). Returns true if drawn, false if not ready.
    bool DrawRoundedImage(ID2D1RenderTarget* rt, const std::wstring& url, const D2D1_RECT_F& destRect, float rx, float ry, float alpha = 1.0f);

    // Returns the cached Direct2D bitmap, creating it from the WIC converter on the UI thread if ready.
    ID2D1Bitmap* GetBitmap(ID2D1RenderTarget* rt, const std::wstring& url);

private:
    ImageCache();
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    HWND m_hwnd = nullptr;
    IWICImagingFactory* m_wicFactory = nullptr;
    std::mutex m_mu;
    std::unordered_map<std::wstring, ImageCacheEntry> m_entries;

    void EnsureWicFactory();
    void TriggerFetch(const std::wstring& url);
    static std::wstring GetDiskCachePath(const std::wstring& url);
    static std::vector<BYTE> ReadDiskFile(const std::wstring& path);
    static void WriteDiskFile(const std::wstring& path, const std::vector<BYTE>& data);
    static std::vector<BYTE> DownloadUrl(const std::wstring& url);
    IWICFormatConverter* DecodeImageMemory(const std::vector<BYTE>& data, UINT32& outW, UINT32& outH);
};
