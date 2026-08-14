#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace cursor_trail
{
    struct ParticleVisual
    {
        Gdiplus::PointF center;
        float rotationDegrees;
        float scale;
        float opacity;
    };

    struct LineVisual
    {
        Gdiplus::PointF start;
        Gdiplus::PointF end;
        float width;
        float opacity;
    };

    class LayeredRenderer final
    {
    public:
        LayeredRenderer();
        ~LayeredRenderer();

        LayeredRenderer(const LayeredRenderer&) = delete;
        LayeredRenderer& operator=(const LayeredRenderer&) = delete;

        bool Initialize(HWND window);
        void SetGlowColor(const Gdiplus::Color& color);
        HICON CreateTrayIcon() const;
        bool Render(const std::vector<ParticleVisual>& particles);
        bool RenderLine(const std::vector<LineVisual>& segments);
        void Hide();
        void KeepTopmost() const;
        bool IsVisible() const noexcept;

    private:
        class LayeredSurface;

        bool LoadDefaultSprite();
        bool BuildThemedSprite();

        HWND window_ = nullptr;
        ULONG_PTR gdiplusToken_ = 0;
        Gdiplus::Color glowColor_ = Gdiplus::Color(255, 255, 0, 0);
        std::unique_ptr<Gdiplus::Bitmap> defaultSprite_;
        std::unique_ptr<Gdiplus::Bitmap> batSprite_;
        std::vector<std::uint32_t> spritePixels_;
        std::unique_ptr<LayeredSurface> surface_;
        bool visible_ = false;
    };
}
