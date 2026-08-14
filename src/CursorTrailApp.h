#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include <chrono>
#include <random>
#include <vector>

#include "LayeredRenderer.h"

namespace cursor_trail
{
    class CursorTrailApp final
    {
    public:
        CursorTrailApp();
        ~CursorTrailApp();

        CursorTrailApp(const CursorTrailApp&) = delete;
        CursorTrailApp& operator=(const CursorTrailApp&) = delete;

        bool Initialize(HINSTANCE instance);
        int Run();

    private:
        enum class Theme
        {
            MikuRed,
            Aqua,
            Violet
        };

        enum class PerformanceMode
        {
            Eco,
            Balanced,
            Smooth
        };

        enum class TrailStyle
        {
            Bat,
            Line
        };

        struct Particle
        {
            Gdiplus::PointF origin;
            Gdiplus::PointF drift;
            float initialRotation;
            float initialScale;
            float dpiScale;
            double bornAt;
        };

        struct TrailPoint
        {
            Gdiplus::PointF position;
            double bornAt;
        };

        static LRESULT CALLBACK WindowProcedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void HandleRawInput(HRAWINPUT inputHandle);
        void OnCursorInput();
        void OnAnimationTick();
        void StartAnimationTimer();
        void StopAnimationTimer();
        void SpawnParticle(const POINT& cursor, double now);
        void AddTrailPoint(const POINT& cursor, double now);
        void RemoveExpiredParticles(double now);
        void RemoveExpiredTrailPoints(double now);
        void BuildVisuals(double now);
        void BuildLineVisuals(double now);
        void ClearTrail();
        bool IsTrailAllowed() const;
        void ShowTrayMenu();
        void ExecuteMenuCommand(UINT command);
        void SetTheme(Theme theme);
        void SetPerformanceMode(PerformanceMode mode);
        void SetTrailStyle(TrailStyle style);
        void TogglePause();
        bool AddTrayIcon();
        void RemoveTrayIcon();
        void RefreshTrayIcon();
        double ElapsedSeconds() const;
        float RandomBetween(float minimum, float maximum);

        HINSTANCE instance_ = nullptr;
        HWND window_ = nullptr;
        HICON trayIcon_ = nullptr;
        UINT taskbarCreatedMessage_ = 0;
        LayeredRenderer renderer_;
        std::vector<Particle> particles_;
        std::vector<TrailPoint> trailPoints_;
        std::vector<ParticleVisual> visuals_;
        std::vector<LineVisual> lineVisuals_;
        std::mt19937 random_;
        std::chrono::steady_clock::time_point startedAt_;
        POINT lastCursor_{};
        HCURSOR arrowCursor_ = nullptr;
        Theme theme_ = Theme::MikuRed;
        PerformanceMode performanceMode_ = PerformanceMode::Eco;
        TrailStyle trailStyle_ = TrailStyle::Line;
        bool hasCursor_ = false;
        bool paused_ = false;
        bool hideOnNonArrowCursor_ = false;
        bool trayIconAdded_ = false;
        bool animationTimerRunning_ = false;
        double lastSpawnAt_ = -1000.0;
        double lastInputSampleAt_ = -1000.0;
        UINT animationIntervalMilliseconds_ = 16;
    };
}
