#include "LayeredRenderer.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <limits>

namespace cursor_trail
{
    namespace
    {
        constexpr float kSpriteAnchorX = 32.0F;
        constexpr float kSpriteAnchorY = 21.0F;
        constexpr int kSurfaceGrid = 64;
        constexpr int kSurfacePadding = 48;

        int FloorToGrid(const int value, const int grid)
        {
            const int remainder = value % grid;
            if (remainder == 0)
            {
                return value;
            }

            return remainder < 0 ? value - remainder - grid : value - remainder;
        }

        int CeilToGrid(const int value, const int grid)
        {
            const int remainder = value % grid;
            if (remainder == 0)
            {
                return value;
            }

            return remainder < 0 ? value - remainder : value - remainder + grid;
        }

        bool Contains(const RECT& outer, const RECT& inner)
        {
            return outer.left <= inner.left && outer.top <= inner.top &&
                outer.right >= inner.right && outer.bottom >= inner.bottom;
        }

        std::int64_t Area(const RECT& rectangle)
        {
            return static_cast<std::int64_t>(rectangle.right - rectangle.left) *
                static_cast<std::int64_t>(rectangle.bottom - rectangle.top);
        }

        RECT ExpandedAndSnapped(RECT rectangle)
        {
            InflateRect(&rectangle, kSurfacePadding, kSurfacePadding);
            rectangle.left = FloorToGrid(rectangle.left, kSurfaceGrid);
            rectangle.top = FloorToGrid(rectangle.top, kSurfaceGrid);
            rectangle.right = CeilToGrid(rectangle.right, kSurfaceGrid);
            rectangle.bottom = CeilToGrid(rectangle.bottom, kSurfaceGrid);
            return rectangle;
        }

        std::unique_ptr<Gdiplus::Bitmap> CloneBitmap(Gdiplus::Bitmap& source)
        {
            Gdiplus::Bitmap* clone = source.Clone(
                0,
                0,
                static_cast<INT>(source.GetWidth()),
                static_cast<INT>(source.GetHeight()),
                PixelFormat32bppARGB);
            if (clone == nullptr || clone->GetLastStatus() != Gdiplus::Ok)
            {
                delete clone;
                return nullptr;
            }
            return std::unique_ptr<Gdiplus::Bitmap>(clone);
        }

        Gdiplus::PointF TransformSpritePoint(
            const Gdiplus::PointF& center,
            const float localX,
            const float localY,
            const float rotationDegrees,
            const float scale)
        {
            constexpr float kDegreesToRadians = 3.14159265358979323846F / 180.0F;
            const float radians = rotationDegrees * kDegreesToRadians;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            return Gdiplus::PointF(
                center.X + ((localX * cosine) - (localY * sine)) * scale,
                center.Y + ((localX * sine) + (localY * cosine)) * scale);
        }

        bool RasterizeParticle(
            const ParticleVisual& particle,
            const std::vector<std::uint32_t>& sourcePixels,
            const int sourceWidth,
            const int sourceHeight,
            std::uint32_t* destinationPixels,
            const int destinationWidth,
            const int destinationHeight,
            const int destinationLeft,
            const int destinationTop)
        {
            if (sourcePixels.empty() || destinationPixels == nullptr || particle.scale <= 0.0F || particle.opacity <= 0.0F)
            {
                return false;
            }

            bool renderedPixel = false;

            const float sourceRight = static_cast<float>(sourceWidth) - kSpriteAnchorX;
            const float sourceBottom = static_cast<float>(sourceHeight) - kSpriteAnchorY;
            const Gdiplus::PointF corners[] = {
                TransformSpritePoint(particle.center, -kSpriteAnchorX, -kSpriteAnchorY, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, sourceRight, -kSpriteAnchorY, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, sourceRight, sourceBottom, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, -kSpriteAnchorX, sourceBottom, particle.rotationDegrees, particle.scale)
            };

            float minimumX = corners[0].X;
            float minimumY = corners[0].Y;
            float maximumX = corners[0].X;
            float maximumY = corners[0].Y;
            for (const Gdiplus::PointF& corner : corners)
            {
                minimumX = std::min(minimumX, corner.X);
                minimumY = std::min(minimumY, corner.Y);
                maximumX = std::max(maximumX, corner.X);
                maximumY = std::max(maximumY, corner.Y);
            }

            const int firstX = std::max(0, static_cast<int>(std::floor(minimumX)) - destinationLeft);
            const int firstY = std::max(0, static_cast<int>(std::floor(minimumY)) - destinationTop);
            const int lastX = std::min(destinationWidth, static_cast<int>(std::ceil(maximumX)) - destinationLeft);
            const int lastY = std::min(destinationHeight, static_cast<int>(std::ceil(maximumY)) - destinationTop);

            constexpr float kDegreesToRadians = 3.14159265358979323846F / 180.0F;
            const float radians = particle.rotationDegrees * kDegreesToRadians;
            const float cosine = std::cos(radians);
            const float sine = std::sin(radians);
            const float inverseScale = 1.0F / particle.scale;
            const float opacity = std::clamp(particle.opacity, 0.0F, 1.0F);

            for (int destinationY = firstY; destinationY < lastY; ++destinationY)
            {
                const float screenY = static_cast<float>(destinationTop + destinationY) + 0.5F;
                for (int destinationX = firstX; destinationX < lastX; ++destinationX)
                {
                    const float screenX = static_cast<float>(destinationLeft + destinationX) + 0.5F;
                    const float deltaX = screenX - particle.center.X;
                    const float deltaY = screenY - particle.center.Y;
                    const float sourceX = ((deltaX * cosine) + (deltaY * sine)) * inverseScale + kSpriteAnchorX - 0.5F;
                    const float sourceY = ((-deltaX * sine) + (deltaY * cosine)) * inverseScale + kSpriteAnchorY - 0.5F;
                    if (sourceX < 0.0F || sourceY < 0.0F ||
                        sourceX > static_cast<float>(sourceWidth - 1) ||
                        sourceY > static_cast<float>(sourceHeight - 1))
                    {
                        continue;
                    }

                    const int x0 = static_cast<int>(sourceX);
                    const int y0 = static_cast<int>(sourceY);
                    const int x1 = std::min(x0 + 1, sourceWidth - 1);
                    const int y1 = std::min(y0 + 1, sourceHeight - 1);
                    const float fractionX = sourceX - static_cast<float>(x0);
                    const float fractionY = sourceY - static_cast<float>(y0);
                    const float weight00 = (1.0F - fractionX) * (1.0F - fractionY);
                    const float weight10 = fractionX * (1.0F - fractionY);
                    const float weight01 = (1.0F - fractionX) * fractionY;
                    const float weight11 = fractionX * fractionY;

                    const std::uint32_t sample00 = sourcePixels[static_cast<std::size_t>(y0 * sourceWidth + x0)];
                    const std::uint32_t sample10 = sourcePixels[static_cast<std::size_t>(y0 * sourceWidth + x1)];
                    const std::uint32_t sample01 = sourcePixels[static_cast<std::size_t>(y1 * sourceWidth + x0)];
                    const std::uint32_t sample11 = sourcePixels[static_cast<std::size_t>(y1 * sourceWidth + x1)];

                    const auto interpolateChannel = [&](const int shift)
                    {
                        return static_cast<unsigned int>(
                            ((((sample00 >> shift) & 0xFFU) * weight00) +
                             (((sample10 >> shift) & 0xFFU) * weight10) +
                             (((sample01 >> shift) & 0xFFU) * weight01) +
                             (((sample11 >> shift) & 0xFFU) * weight11)) * opacity + 0.5F);
                    };

                    const unsigned int sourceBlue = interpolateChannel(0);
                    const unsigned int sourceGreen = interpolateChannel(8);
                    const unsigned int sourceRed = interpolateChannel(16);
                    const unsigned int sourceAlpha = interpolateChannel(24);
                    if (sourceAlpha == 0)
                    {
                        continue;
                    }

                    std::uint32_t& destination = destinationPixels[
                        static_cast<std::size_t>(destinationY * destinationWidth + destinationX)];
                    const unsigned int inverseAlpha = 255U - sourceAlpha;
                    const unsigned int destinationBlue = destination & 0xFFU;
                    const unsigned int destinationGreen = (destination >> 8) & 0xFFU;
                    const unsigned int destinationRed = (destination >> 16) & 0xFFU;
                    const unsigned int destinationAlpha = (destination >> 24) & 0xFFU;

                    const unsigned int outputBlue = sourceBlue + ((destinationBlue * inverseAlpha + 127U) / 255U);
                    const unsigned int outputGreen = sourceGreen + ((destinationGreen * inverseAlpha + 127U) / 255U);
                    const unsigned int outputRed = sourceRed + ((destinationRed * inverseAlpha + 127U) / 255U);
                    const unsigned int outputAlpha = sourceAlpha + ((destinationAlpha * inverseAlpha + 127U) / 255U);
                    destination =
                        (std::min(outputAlpha, 255U) << 24) |
                        (std::min(outputRed, 255U) << 16) |
                        (std::min(outputGreen, 255U) << 8) |
                        std::min(outputBlue, 255U);
                    renderedPixel = true;
                }
            }
            return renderedPixel;
        }

        // Rasterize a single anti-aliased stroke segment (rounded cap) into the
        // premultiplied destination buffer. Colors are the same glow color at full
        // opacity, scaled by the segment's opacity, so a trail reads as a smooth
        // glowing line without the cost of per-pixel gradients.
        bool BlendLineSegment(
            const LineVisual& segment,
            const std::uint32_t glowColorPremultiplied,
            std::uint32_t* destinationPixels,
            const int destinationWidth,
            const int destinationHeight,
            const int destinationLeft,
            const int destinationTop)
        {
            if (segment.width <= 0.0F || segment.opacity <= 0.0F)
            {
                return false;
            }

            const float dx = segment.end.X - segment.start.X;
            const float dy = segment.end.Y - segment.start.Y;
            const float lengthSquared = (dx * dx) + (dy * dy);
            if (lengthSquared < 0.25F)
            {
                return false;
            }

            const float radius = segment.width * 0.5F;
            const float left = std::min(segment.start.X, segment.end.X) - radius;
            const float top = std::min(segment.start.Y, segment.end.Y) - radius;
            const float right = std::max(segment.start.X, segment.end.X) + radius;
            const float bottom = std::max(segment.start.Y, segment.end.Y) + radius;

            const int firstX = std::max(0, static_cast<int>(std::floor(left)) - destinationLeft);
            const int firstY = std::max(0, static_cast<int>(std::floor(top)) - destinationTop);
            const int lastX = std::min(destinationWidth, static_cast<int>(std::ceil(right)) - destinationLeft);
            const int lastY = std::min(destinationHeight, static_cast<int>(std::ceil(bottom)) - destinationTop);

            const float opacity = std::clamp(segment.opacity, 0.0F, 1.0F);
            const unsigned int sourceRed = ((glowColorPremultiplied >> 16) & 0xFFU);
            const unsigned int sourceGreen = ((glowColorPremultiplied >> 8) & 0xFFU);
            const unsigned int sourceBlue = (glowColorPremultiplied & 0xFFU);
            const unsigned int sourceAlpha = ((glowColorPremultiplied >> 24) & 0xFFU);

            bool renderedPixel = false;
            const float falloff = std::max(0.35F, radius - 0.5F);

            for (int destinationY = firstY; destinationY < lastY; ++destinationY)
            {
                const float screenY = static_cast<float>(destinationTop + destinationY) + 0.5F;
                for (int destinationX = firstX; destinationX < lastX; ++destinationX)
                {
                    const float screenX = static_cast<float>(destinationLeft + destinationX) + 0.5F;

                    const float toX = screenX - segment.start.X;
                    const float toY = screenY - segment.start.Y;
                    const float projection = (toX * dx + toY * dy) / lengthSquared;
                    const float clamped = std::clamp(projection, 0.0F, 1.0F);
                    const float closestX = segment.start.X + clamped * dx;
                    const float closestY = segment.start.Y + clamped * dy;
                    const float offsetX = screenX - closestX;
                    const float offsetY = screenY - closestY;
                    const float distance = std::sqrt((offsetX * offsetX) + (offsetY * offsetY));

                    const float coverage = std::clamp((radius - distance) / falloff, 0.0F, 1.0F);
                    if (coverage <= 0.0F)
                    {
                        continue;
                    }

                    const unsigned int blendAlpha = static_cast<unsigned int>(sourceAlpha * coverage * opacity + 0.5F);
                    if (blendAlpha == 0)
                    {
                        continue;
                    }

                    std::uint32_t& destination = destinationPixels[
                        static_cast<std::size_t>(destinationY * destinationWidth + destinationX)];
                    const unsigned int inverseAlpha = 255U - blendAlpha;
                    const unsigned int destinationBlue = destination & 0xFFU;
                    const unsigned int destinationGreen = (destination >> 8) & 0xFFU;
                    const unsigned int destinationRed = (destination >> 16) & 0xFFU;
                    const unsigned int destinationAlpha = (destination >> 24) & 0xFFU;

                    const unsigned int outputBlue = (sourceBlue * blendAlpha + destinationBlue * inverseAlpha + 127U) / 255U;
                    const unsigned int outputGreen = (sourceGreen * blendAlpha + destinationGreen * inverseAlpha + 127U) / 255U;
                    const unsigned int outputRed = (sourceRed * blendAlpha + destinationRed * inverseAlpha + 127U) / 255U;
                    const unsigned int outputAlpha = (blendAlpha + destinationAlpha * inverseAlpha + 127U) / 255U;
                    destination =
                        (std::min(outputAlpha, 255U) << 24) |
                        (std::min(outputRed, 255U) << 16) |
                        (std::min(outputGreen, 255U) << 8) |
                        std::min(outputBlue, 255U);
                    renderedPixel = true;
                }
            }
            return renderedPixel;
        }
    }

    class LayeredRenderer::LayeredSurface final
    {
    public:
        ~LayeredSurface()
        {
            Reset();
        }

        LayeredSurface(const LayeredSurface&) = delete;
        LayeredSurface& operator=(const LayeredSurface&) = delete;

        LayeredSurface() = default;

        bool Ensure(const RECT& contentBounds)
        {
            const RECT requested = ExpandedAndSnapped(contentBounds);
            const std::int64_t requestedArea = std::max<std::int64_t>(1, Area(requested));

            if (bitmap_ != nullptr && Contains(bounds_, contentBounds) && Area(bounds_) <= requestedArea * 3)
            {
                return true;
            }

            Reset();

            bounds_ = requested;
            width_ = bounds_.right - bounds_.left;
            height_ = bounds_.bottom - bounds_.top;
            if (width_ <= 0 || height_ <= 0)
            {
                return false;
            }

            BITMAPINFO bitmapInfo{};
            bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
            bitmapInfo.bmiHeader.biWidth = width_;
            bitmapInfo.bmiHeader.biHeight = -height_;
            bitmapInfo.bmiHeader.biPlanes = 1;
            bitmapInfo.bmiHeader.biBitCount = 32;
            bitmapInfo.bmiHeader.biCompression = BI_RGB;

            memoryDc_ = CreateCompatibleDC(nullptr);
            if (memoryDc_ == nullptr)
            {
                Reset();
                return false;
            }

            bitmap_ = CreateDIBSection(
                memoryDc_,
                &bitmapInfo,
                DIB_RGB_COLORS,
                &pixels_,
                nullptr,
                0);
            if (bitmap_ == nullptr || pixels_ == nullptr)
            {
                Reset();
                return false;
            }

            previousBitmap_ = SelectObject(memoryDc_, bitmap_);
            return true;
        }

        void Reset()
        {
            if (memoryDc_ != nullptr && previousBitmap_ != nullptr)
            {
                SelectObject(memoryDc_, previousBitmap_);
            }

            if (bitmap_ != nullptr)
            {
                DeleteObject(bitmap_);
            }

            if (memoryDc_ != nullptr)
            {
                DeleteDC(memoryDc_);
            }

            memoryDc_ = nullptr;
            bitmap_ = nullptr;
            previousBitmap_ = nullptr;
            pixels_ = nullptr;
            width_ = 0;
            height_ = 0;
            bounds_ = {};
        }

        std::uint32_t* Pixels() const noexcept
        {
            return static_cast<std::uint32_t*>(pixels_);
        }

        int Width() const noexcept
        {
            return width_;
        }

        int Height() const noexcept
        {
            return height_;
        }

        const RECT& Bounds() const noexcept
        {
            return bounds_;
        }

        bool Present(const HWND window) const
        {
            if (memoryDc_ == nullptr)
            {
                return false;
            }

            HDC screenDc = GetDC(nullptr);
            if (screenDc == nullptr)
            {
                return false;
            }

            POINT destination{ bounds_.left, bounds_.top };
            SIZE size{ width_, height_ };
            POINT source{ 0, 0 };
            BLENDFUNCTION blend{};
            blend.BlendOp = AC_SRC_OVER;
            blend.SourceConstantAlpha = 255;
            blend.AlphaFormat = AC_SRC_ALPHA;

            const BOOL updated = UpdateLayeredWindow(
                window,
                screenDc,
                &destination,
                &size,
                memoryDc_,
                &source,
                0,
                &blend,
                ULW_ALPHA);

            ReleaseDC(nullptr, screenDc);
            return updated != FALSE;
        }

    private:
        HDC memoryDc_ = nullptr;
        HBITMAP bitmap_ = nullptr;
        HGDIOBJ previousBitmap_ = nullptr;
        void* pixels_ = nullptr;
        int width_ = 0;
        int height_ = 0;
        RECT bounds_{};
    };

    LayeredRenderer::LayeredRenderer() = default;

    LayeredRenderer::~LayeredRenderer()
    {
        Hide();
        surface_.reset();
        batSprite_.reset();
        defaultSprite_.reset();
        spritePixels_.clear();

        if (gdiplusToken_ != 0)
        {
            Gdiplus::GdiplusShutdown(gdiplusToken_);
        }
    }

    bool LayeredRenderer::Initialize(const HWND window)
    {
        window_ = window;

        Gdiplus::GdiplusStartupInput startupInput;
        if (Gdiplus::GdiplusStartup(&gdiplusToken_, &startupInput, nullptr) != Gdiplus::Ok)
        {
            return false;
        }

        surface_ = std::make_unique<LayeredSurface>();
        return LoadDefaultSprite() && BuildThemedSprite();
    }

    void LayeredRenderer::SetGlowColor(const Gdiplus::Color& color)
    {
        glowColor_ = color;
        BuildThemedSprite();
    }

    bool LayeredRenderer::LoadDefaultSprite()
    {
        HRSRC resource = FindResourceW(
            GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDR_BAT_DEFAULT),
            RT_RCDATA);
        if (resource == nullptr)
        {
            return false;
        }

        HGLOBAL loadedResource = LoadResource(GetModuleHandleW(nullptr), resource);
        const DWORD resourceSize = SizeofResource(GetModuleHandleW(nullptr), resource);
        const void* resourceBytes = loadedResource == nullptr ? nullptr : LockResource(loadedResource);
        if (resourceBytes == nullptr || resourceSize == 0)
        {
            return false;
        }

        HGLOBAL streamMemory = GlobalAlloc(GMEM_MOVEABLE, resourceSize);
        if (streamMemory == nullptr)
        {
            return false;
        }

        void* streamBytes = GlobalLock(streamMemory);
        if (streamBytes == nullptr)
        {
            GlobalFree(streamMemory);
            return false;
        }
        std::memcpy(streamBytes, resourceBytes, resourceSize);
        GlobalUnlock(streamMemory);

        IStream* stream = nullptr;
        if (CreateStreamOnHGlobal(streamMemory, TRUE, &stream) != S_OK)
        {
            GlobalFree(streamMemory);
            return false;
        }

        std::unique_ptr<Gdiplus::Bitmap> decoded(Gdiplus::Bitmap::FromStream(stream, FALSE));
        if (decoded == nullptr || decoded->GetLastStatus() != Gdiplus::Ok)
        {
            stream->Release();
            return false;
        }

        defaultSprite_ = CloneBitmap(*decoded);
        decoded.reset();
        stream->Release();
        return defaultSprite_ != nullptr;
    }

    bool LayeredRenderer::BuildThemedSprite()
    {
        if (defaultSprite_ == nullptr)
        {
            return false;
        }

        auto sprite = CloneBitmap(*defaultSprite_);
        if (sprite == nullptr)
        {
            return false;
        }

        const BYTE targetRed = glowColor_.GetR();
        const BYTE targetGreen = glowColor_.GetG();
        const BYTE targetBlue = glowColor_.GetB();
        if (targetRed != 255 || targetGreen != 0 || targetBlue != 0)
        {
            const Gdiplus::Rect bounds(
                0,
                0,
                static_cast<INT>(sprite->GetWidth()),
                static_cast<INT>(sprite->GetHeight()));
            Gdiplus::BitmapData data{};
            if (sprite->LockBits(
                    &bounds,
                    Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
                    PixelFormat32bppARGB,
                    &data) != Gdiplus::Ok)
            {
                return false;
            }

            for (UINT y = 0; y < sprite->GetHeight(); ++y)
            {
                BYTE* row = static_cast<BYTE*>(data.Scan0) + (static_cast<INT>(y) * data.Stride);
                for (UINT x = 0; x < sprite->GetWidth(); ++x)
                {
                    BYTE* pixel = row + (x * 4);
                    const BYTE blue = pixel[0];
                    const BYTE green = pixel[1];
                    const BYTE red = pixel[2];
                    if (red > green + 8 && red > blue + 8)
                    {
                        pixel[0] = static_cast<BYTE>((static_cast<UINT>(targetBlue) * red) / 255U);
                        pixel[1] = static_cast<BYTE>((static_cast<UINT>(targetGreen) * red) / 255U);
                        pixel[2] = static_cast<BYTE>((static_cast<UINT>(targetRed) * red) / 255U);
                    }
                }
            }
            sprite->UnlockBits(&data);
        }

        const UINT spriteWidth = sprite->GetWidth();
        const UINT spriteHeight = sprite->GetHeight();
        const Gdiplus::Rect pixelBounds(
            0,
            0,
            static_cast<INT>(spriteWidth),
            static_cast<INT>(spriteHeight));
        Gdiplus::BitmapData premultipliedData{};
        if (sprite->LockBits(
                &pixelBounds,
                Gdiplus::ImageLockModeRead,
                PixelFormat32bppPARGB,
                &premultipliedData) != Gdiplus::Ok)
        {
            return false;
        }

        spritePixels_.resize(static_cast<std::size_t>(spriteWidth) * spriteHeight);
        for (UINT y = 0; y < spriteHeight; ++y)
        {
            const BYTE* sourceRow = static_cast<const BYTE*>(premultipliedData.Scan0) +
                (static_cast<INT>(y) * premultipliedData.Stride);
            std::memcpy(
                spritePixels_.data() + (static_cast<std::size_t>(y) * spriteWidth),
                sourceRow,
                static_cast<std::size_t>(spriteWidth) * sizeof(std::uint32_t));
        }
        sprite->UnlockBits(&premultipliedData);

        batSprite_ = std::move(sprite);
        return true;
    }

    HICON LayeredRenderer::CreateTrayIcon() const
    {
        if (batSprite_ == nullptr)
        {
            return nullptr;
        }

        Gdiplus::Bitmap iconBitmap(32, 32, PixelFormat32bppPARGB);
        Gdiplus::Graphics graphics(&iconBitmap);
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
        graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
        graphics.SetCompositingMode(Gdiplus::CompositingModeSourceOver);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(batSprite_.get(), Gdiplus::Rect(0, 0, 32, 32));

        HICON icon = nullptr;
        return iconBitmap.GetHICON(&icon) == Gdiplus::Ok ? icon : nullptr;
    }

    bool LayeredRenderer::Render(const std::vector<ParticleVisual>& particles)
    {
        if (window_ == nullptr || surface_ == nullptr || batSprite_ == nullptr || particles.empty())
        {
            Hide();
            return true;
        }

        float left = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float bottom = std::numeric_limits<float>::lowest();

        const float spriteRight = static_cast<float>(batSprite_->GetWidth()) - kSpriteAnchorX;
        const float spriteBottom = static_cast<float>(batSprite_->GetHeight()) - kSpriteAnchorY;
        for (const ParticleVisual& particle : particles)
        {
            const Gdiplus::PointF corners[] = {
                TransformSpritePoint(particle.center, -kSpriteAnchorX, -kSpriteAnchorY, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, spriteRight, -kSpriteAnchorY, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, spriteRight, spriteBottom, particle.rotationDegrees, particle.scale),
                TransformSpritePoint(particle.center, -kSpriteAnchorX, spriteBottom, particle.rotationDegrees, particle.scale)
            };
            for (const Gdiplus::PointF& corner : corners)
            {
                left = std::min(left, corner.X);
                top = std::min(top, corner.Y);
                right = std::max(right, corner.X);
                bottom = std::max(bottom, corner.Y);
            }
        }

        RECT contentBounds{
            static_cast<LONG>(std::floor(left)),
            static_cast<LONG>(std::floor(top)),
            static_cast<LONG>(std::ceil(right)),
            static_cast<LONG>(std::ceil(bottom))
        };

        if (!surface_->Ensure(contentBounds))
        {
            Hide();
            return false;
        }

        const RECT& surfaceBounds = surface_->Bounds();
        std::uint32_t* destinationPixels = surface_->Pixels();
        const int destinationWidth = surface_->Width();
        const int destinationHeight = surface_->Height();
        std::memset(
            destinationPixels,
            0,
            static_cast<std::size_t>(destinationWidth) * destinationHeight * sizeof(std::uint32_t));

        const int spriteWidth = static_cast<int>(batSprite_->GetWidth());
        const int spriteHeight = static_cast<int>(batSprite_->GetHeight());
        bool renderedPixel = false;
        for (const ParticleVisual& particle : particles)
        {
            renderedPixel = RasterizeParticle(
                particle,
                spritePixels_,
                spriteWidth,
                spriteHeight,
                destinationPixels,
                destinationWidth,
                destinationHeight,
                surfaceBounds.left,
                surfaceBounds.top) || renderedPixel;
        }

        if (!renderedPixel)
        {
            Hide();
            return true;
        }

        if (!surface_->Present(window_))
        {
            Hide();
            return false;
        }

        if (!visible_)
        {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            visible_ = true;
        }

        return true;
    }

    bool LayeredRenderer::RenderLine(const std::vector<LineVisual>& segments)
    {
        if (window_ == nullptr || surface_ == nullptr || segments.empty())
        {
            Hide();
            return true;
        }

        // Premultiply the glow color once so segment blending stays cheap.
        const unsigned int alpha = glowColor_.GetA();
        const unsigned int red = (glowColor_.GetR() * alpha + 127U) / 255U;
        const unsigned int green = (glowColor_.GetG() * alpha + 127U) / 255U;
        const unsigned int blue = (glowColor_.GetB() * alpha + 127U) / 255U;
        const std::uint32_t premultiplied = (alpha << 24) | (red << 16) | (green << 8) | blue;

        float left = std::numeric_limits<float>::max();
        float top = std::numeric_limits<float>::max();
        float right = std::numeric_limits<float>::lowest();
        float bottom = std::numeric_limits<float>::lowest();
        for (const LineVisual& segment : segments)
        {
            const float radius = segment.width * 0.5F;
            left = std::min(left, std::min(segment.start.X, segment.end.X) - radius);
            top = std::min(top, std::min(segment.start.Y, segment.end.Y) - radius);
            right = std::max(right, std::max(segment.start.X, segment.end.X) + radius);
            bottom = std::max(bottom, std::max(segment.start.Y, segment.end.Y) + radius);
        }

        RECT contentBounds{
            static_cast<LONG>(std::floor(left)),
            static_cast<LONG>(std::floor(top)),
            static_cast<LONG>(std::ceil(right)),
            static_cast<LONG>(std::ceil(bottom))
        };

        if (!surface_->Ensure(contentBounds))
        {
            Hide();
            return false;
        }

        const RECT& surfaceBounds = surface_->Bounds();
        std::uint32_t* destinationPixels = surface_->Pixels();
        const int destinationWidth = surface_->Width();
        const int destinationHeight = surface_->Height();
        std::memset(
            destinationPixels,
            0,
            static_cast<std::size_t>(destinationWidth) * destinationHeight * sizeof(std::uint32_t));

        bool renderedPixel = false;
        for (const LineVisual& segment : segments)
        {
            renderedPixel = BlendLineSegment(
                segment,
                premultiplied,
                destinationPixels,
                destinationWidth,
                destinationHeight,
                surfaceBounds.left,
                surfaceBounds.top) || renderedPixel;
        }

        if (!renderedPixel)
        {
            Hide();
            return true;
        }

        if (!surface_->Present(window_))
        {
            Hide();
            return false;
        }

        if (!visible_)
        {
            ShowWindow(window_, SW_SHOWNOACTIVATE);
            visible_ = true;
        }

        return true;
    }

    void LayeredRenderer::Hide()
    {
        if (visible_ && window_ != nullptr)
        {
            ShowWindow(window_, SW_HIDE);
        }

        visible_ = false;
        if (surface_ != nullptr)
        {
            surface_->Reset();
        }
    }

    void LayeredRenderer::KeepTopmost() const
    {
        if (visible_ && window_ != nullptr)
        {
            SetWindowPos(
                window_,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }
    }

    bool LayeredRenderer::IsVisible() const noexcept
    {
        return visible_;
    }
}
