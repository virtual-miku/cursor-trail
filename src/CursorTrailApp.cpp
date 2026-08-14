#include "CursorTrailApp.h"
#include "resource.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <shellapi.h>

namespace cursor_trail
{
    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"CursorTrail.NativeOverlay";
        constexpr wchar_t kWindowTitle[] = L"Cursor Trail";
        constexpr UINT kTrayIconId = 1;
        constexpr UINT kTrayCallbackMessage = WM_APP + 1;
        constexpr UINT_PTR kAnimationTimerId = 1;
        constexpr double kParticleLifetimeSeconds = 0.70;
        constexpr double kSpawnIntervalSeconds = 0.040;
        constexpr double kInputSampleIntervalSeconds = 0.008;
        constexpr std::size_t kMaximumParticles = 24;
        constexpr double kLineLifetimeSeconds = 0.92;
        constexpr float kLineMinimumDistance = 2.5F;
        constexpr float kLineTeleportDistance = 180.0F;
        constexpr float kLineGlowWidth = 12.5F;
        constexpr float kLineCoreWidth = 2.0F;
        constexpr std::size_t kMaximumTrailPoints = 150;

        constexpr UINT kCommandPause = 100;
        constexpr UINT kCommandThemeRed = 200;
        constexpr UINT kCommandThemeAqua = 201;
        constexpr UINT kCommandThemeViolet = 202;
        constexpr UINT kCommandPerformanceEco = 300;
        constexpr UINT kCommandPerformanceBalanced = 301;
        constexpr UINT kCommandPerformanceSmooth = 302;
        constexpr UINT kCommandHideOnNonArrow = 400;
        constexpr UINT kCommandStyleBat = 500;
        constexpr UINT kCommandStyleLine = 501;
        constexpr UINT kCommandExit = 900;

        float DpiScaleAtPoint(const POINT& point)
        {
            using GetDpiForMonitorFunction = HRESULT(WINAPI*)(HMONITOR, int, UINT*, UINT*);
            static HMODULE shcore = LoadLibraryExW(
                L"shcore.dll",
                nullptr,
                LOAD_LIBRARY_SEARCH_SYSTEM32);
            static const auto getDpiForMonitor = shcore == nullptr
                ? nullptr
                : reinterpret_cast<GetDpiForMonitorFunction>(GetProcAddress(shcore, "GetDpiForMonitor"));

            if (getDpiForMonitor != nullptr)
            {
                UINT dpiX = 96;
                UINT dpiY = 96;
                const HMONITOR monitor = MonitorFromPoint(point, MONITOR_DEFAULTTONEAREST);
                if (SUCCEEDED(getDpiForMonitor(monitor, 0, &dpiX, &dpiY)))
                {
                    return static_cast<float>(dpiX) / 96.0F;
                }
            }

            using GetDpiForSystemFunction = UINT(WINAPI*)();
            static const auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFunction>(
                GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForSystem"));
            return getDpiForSystem == nullptr
                ? 1.0F
                : static_cast<float>(getDpiForSystem()) / 96.0F;
        }

        float SmoothStep(float value)
        {
            value = std::clamp(value, 0.0F, 1.0F);
            return value * value * (3.0F - 2.0F * value);
        }

        float CssEaseOut(const float value)
        {
            const float x = std::clamp(value, 0.0F, 1.0F);
            float parameter = x;
            constexpr float xCoefficientB = 1.74F;
            constexpr float xCoefficientA = -0.74F;

            for (int iteration = 0; iteration < 8; ++iteration)
            {
                const float estimatedX = ((xCoefficientA * parameter + xCoefficientB) * parameter) * parameter - x;
                if (std::abs(estimatedX) < 0.000001F)
                {
                    break;
                }

                const float derivative = (3.0F * xCoefficientA * parameter + 2.0F * xCoefficientB) * parameter;
                if (std::abs(derivative) < 0.000001F)
                {
                    break;
                }
                parameter = std::clamp(parameter - (estimatedX / derivative), 0.0F, 1.0F);
            }

            return ((-2.0F * parameter + 3.0F) * parameter) * parameter;
        }

        void AppendCheckedMenuItem(HMENU menu, const UINT command, const wchar_t* label, const bool checked)
        {
            AppendMenuW(menu, MF_STRING | (checked ? MF_CHECKED : MF_UNCHECKED), command, label);
        }
    }

    CursorTrailApp::CursorTrailApp()
        : random_(std::random_device{}()),
          startedAt_(std::chrono::steady_clock::now())
    {
        particles_.reserve(kMaximumParticles);
        visuals_.reserve(kMaximumParticles);
        trailPoints_.reserve(kMaximumTrailPoints);
        lineVisuals_.reserve(kMaximumTrailPoints);
    }

    CursorTrailApp::~CursorTrailApp()
    {
        RemoveTrayIcon();
        if (trayIcon_ != nullptr)
        {
            DestroyIcon(trayIcon_);
            trayIcon_ = nullptr;
        }

        if (window_ != nullptr && IsWindow(window_))
        {
            DestroyWindow(window_);
        }
    }

    bool CursorTrailApp::Initialize(const HINSTANCE instance)
    {
        instance_ = instance;
        arrowCursor_ = LoadCursorW(nullptr, IDC_ARROW);
        taskbarCreatedMessage_ = RegisterWindowMessageW(L"TaskbarCreated");

        const HICON appIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));

        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.hInstance = instance_;
        windowClass.lpfnWndProc = WindowProcedure;
        windowClass.lpszClassName = kWindowClassName;
        windowClass.hCursor = arrowCursor_;
        windowClass.hIcon = appIcon;
        windowClass.hIconSm = appIcon;

        if (RegisterClassExW(&windowClass) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        window_ = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kWindowClassName,
            kWindowTitle,
            WS_POPUP,
            0,
            0,
            1,
            1,
            nullptr,
            nullptr,
            instance_,
            this);
        if (window_ == nullptr)
        {
            return false;
        }

        if (!renderer_.Initialize(window_))
        {
            return false;
        }

        SetTheme(theme_);
        SetPerformanceMode(performanceMode_);
        if (!AddTrayIcon())
        {
            return false;
        }

        RAWINPUTDEVICE mouseDevice{};
        mouseDevice.usUsagePage = 0x01;
        mouseDevice.usUsage = 0x02;
        mouseDevice.dwFlags = RIDEV_INPUTSINK;
        mouseDevice.hwndTarget = window_;
        if (RegisterRawInputDevices(&mouseDevice, 1, sizeof(mouseDevice)) == FALSE)
        {
            return false;
        }

        hasCursor_ = GetCursorPos(&lastCursor_) != FALSE;
        return true;
    }

    int CursorTrailApp::Run()
    {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }

        return static_cast<int>(message.wParam);
    }

    LRESULT CALLBACK CursorTrailApp::WindowProcedure(
        const HWND window,
        const UINT message,
        const WPARAM wParam,
        const LPARAM lParam)
    {
        CursorTrailApp* app = reinterpret_cast<CursorTrailApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            app = static_cast<CursorTrailApp*>(create->lpCreateParams);
            app->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
        }

        if (app != nullptr)
        {
            return app->HandleMessage(message, wParam, lParam);
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT CursorTrailApp::HandleMessage(const UINT message, const WPARAM wParam, const LPARAM lParam)
    {
        if (taskbarCreatedMessage_ != 0 && message == taskbarCreatedMessage_)
        {
            trayIconAdded_ = false;
            AddTrayIcon();
            return 0;
        }

        switch (message)
        {
        case WM_TIMER:
            if (wParam == kAnimationTimerId)
            {
                OnAnimationTick();
            }
            return 0;

        case WM_INPUT:
            HandleRawInput(reinterpret_cast<HRAWINPUT>(lParam));
            return DefWindowProcW(window_, message, wParam, lParam);

        case kTrayCallbackMessage:
            if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
            {
                ShowTrayMenu();
            }
            else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
            {
                TogglePause();
            }
            return 0;

        case WM_DISPLAYCHANGE:
            ClearTrail();
            return 0;

        case WM_NCHITTEST:
            return HTTRANSPARENT;

        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT paint{};
            BeginPaint(window_, &paint);
            EndPaint(window_, &paint);
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(window_);
            return 0;

        case WM_DESTROY:
            StopAnimationTimer();
            renderer_.Hide();
            RemoveTrayIcon();
            window_ = nullptr;
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window_, message, wParam, lParam);
        }
    }

    void CursorTrailApp::HandleRawInput(const HRAWINPUT inputHandle)
    {
        RAWINPUT input{};
        UINT inputSize = sizeof(input);
        const UINT bytesRead = GetRawInputData(
            inputHandle,
            RID_INPUT,
            &input,
            &inputSize,
            sizeof(RAWINPUTHEADER));

        if (bytesRead == static_cast<UINT>(-1) || input.header.dwType != RIM_TYPEMOUSE)
        {
            return;
        }

        const RAWMOUSE& mouse = input.data.mouse;
        const bool moved = (mouse.usFlags & MOUSE_MOVE_ABSOLUTE) != 0 || mouse.lLastX != 0 || mouse.lLastY != 0;
        if (moved)
        {
            OnCursorInput();
        }
    }

    void CursorTrailApp::OnCursorInput()
    {
        const double now = ElapsedSeconds();
        if (now - lastInputSampleAt_ < kInputSampleIntervalSeconds)
        {
            return;
        }
        lastInputSampleAt_ = now;

        POINT cursor{};
        if (!GetCursorPos(&cursor))
        {
            return;
        }

        if (!hasCursor_)
        {
            lastCursor_ = cursor;
            hasCursor_ = true;
        }

        if (cursor.x == lastCursor_.x && cursor.y == lastCursor_.y)
        {
            return;
        }

        if (paused_ || !IsTrailAllowed())
        {
            lastCursor_ = cursor;
            ClearTrail();
            return;
        }

        if (trailStyle_ == TrailStyle::Line)
        {
            const float distanceX = static_cast<float>(cursor.x - lastCursor_.x);
            const float distanceY = static_cast<float>(cursor.y - lastCursor_.y);
            const float distance = std::sqrt((distanceX * distanceX) + (distanceY * distanceY));
            if (distance >= kLineTeleportDistance)
            {
                trailPoints_.clear();
                AddTrailPoint(cursor, now);
                lastSpawnAt_ = now;
                BuildLineVisuals(now);
                renderer_.RenderLine(lineVisuals_);
                StartAnimationTimer();
            }
            else if (distance >= kLineMinimumDistance)
            {
                const bool firstPoint = trailPoints_.empty();
                AddTrailPoint(cursor, now);
                lastSpawnAt_ = now;

                if (firstPoint)
                {
                    StartAnimationTimer();
                }
            }
        }
        else if (now - lastSpawnAt_ >= kSpawnIntervalSeconds)
        {
            const bool firstParticle = particles_.empty();
            SpawnParticle(cursor, now);
            lastSpawnAt_ = now;

            if (firstParticle)
            {
                BuildVisuals(now);
                renderer_.Render(visuals_);
                StartAnimationTimer();
            }
        }

        lastCursor_ = cursor;
    }

    void CursorTrailApp::OnAnimationTick()
    {
        const double now = ElapsedSeconds();
        if (trailStyle_ == TrailStyle::Line)
        {
            RemoveExpiredTrailPoints(now);
            if (trailPoints_.empty())
            {
                renderer_.Hide();
                StopAnimationTimer();
                return;
            }

            BuildLineVisuals(now);
            renderer_.RenderLine(lineVisuals_);
        }
        else
        {
            RemoveExpiredParticles(now);
            if (particles_.empty())
            {
                renderer_.Hide();
                StopAnimationTimer();
                return;
            }

            BuildVisuals(now);
            renderer_.Render(visuals_);
        }

        // Re-assert topmost on every frame so the trail stays above other
        // topmost windows that may also be fighting for the same Z band.
        renderer_.KeepTopmost();
    }

    void CursorTrailApp::StartAnimationTimer()
    {
        if (window_ == nullptr || animationTimerRunning_)
        {
            return;
        }

        animationTimerRunning_ = SetTimer(
            window_,
            kAnimationTimerId,
            animationIntervalMilliseconds_,
            nullptr) != 0;
    }

    void CursorTrailApp::StopAnimationTimer()
    {
        if (window_ != nullptr && animationTimerRunning_)
        {
            KillTimer(window_, kAnimationTimerId);
        }
        animationTimerRunning_ = false;
    }

    void CursorTrailApp::SpawnParticle(const POINT& cursor, const double now)
    {
        if (particles_.size() >= kMaximumParticles)
        {
            particles_.erase(particles_.begin());
        }

        particles_.push_back(Particle{
            Gdiplus::PointF(static_cast<float>(cursor.x), static_cast<float>(cursor.y)),
            Gdiplus::PointF(RandomBetween(-22.5F, 22.5F), RandomBetween(-22.5F, 22.5F)),
            RandomBetween(-40.0F, 40.0F),
            RandomBetween(0.45F, 0.80F),
            DpiScaleAtPoint(cursor),
            now
        });
    }

    void CursorTrailApp::AddTrailPoint(const POINT& cursor, const double now)
    {
        if (trailPoints_.size() >= kMaximumTrailPoints)
        {
            trailPoints_.erase(trailPoints_.begin());
        }

        trailPoints_.push_back(TrailPoint{
            Gdiplus::PointF(static_cast<float>(cursor.x), static_cast<float>(cursor.y)),
            now
        });
    }

    void CursorTrailApp::RemoveExpiredTrailPoints(const double now)
    {
        trailPoints_.erase(
            std::remove_if(
                trailPoints_.begin(),
                trailPoints_.end(),
                [now](const TrailPoint& point)
                {
                    return now - point.bornAt >= kLineLifetimeSeconds;
                }),
            trailPoints_.end());
    }

    void CursorTrailApp::RemoveExpiredParticles(const double now)
    {
        particles_.erase(
            std::remove_if(
                particles_.begin(),
                particles_.end(),
                [now](const Particle& particle)
                {
                    return now - particle.bornAt >= kParticleLifetimeSeconds;
                }),
            particles_.end());
    }

    void CursorTrailApp::BuildVisuals(const double now)
    {
        visuals_.clear();
        for (const Particle& particle : particles_)
        {
            const float progress = static_cast<float>((now - particle.bornAt) / kParticleLifetimeSeconds);
            const float eased = CssEaseOut(progress);
            const float scale =
                (particle.initialScale + ((0.20F - particle.initialScale) * eased)) * particle.dpiScale;

            visuals_.push_back(ParticleVisual{
                Gdiplus::PointF(
                    particle.origin.X + (particle.drift.X * eased * particle.dpiScale),
                    particle.origin.Y + (particle.drift.Y * eased * particle.dpiScale)),
                particle.initialRotation + (45.0F * eased),
                scale,
                0.95F * (1.0F - eased)
            });
        }
    }

    void CursorTrailApp::BuildLineVisuals(const double now)
    {
        lineVisuals_.clear();
        if (trailPoints_.size() < 2)
        {
            return;
        }

        const std::size_t count = trailPoints_.size();
        for (std::size_t i = 1; i < count; ++i)
        {
            const TrailPoint& a = trailPoints_[i - 1];
            const TrailPoint& b = trailPoints_[i];

            const float age = static_cast<float>(
                std::max(now - a.bornAt, now - b.bornAt));
            const float fade = SmoothStep(1.0F - age / static_cast<float>(kLineLifetimeSeconds));
            if (fade <= 0.01F)
            {
                continue;
            }

            const float headAmount = static_cast<float>(i) / static_cast<float>(count - 1);
            const float taper = 0.72F + SmoothStep(headAmount) * 0.55F;

            // Glow strand (outer soft line).
            lineVisuals_.push_back(LineVisual{
                a.position,
                b.position,
                kLineGlowWidth * taper,
                0.28F * fade
            });

            // Core strand (bright inner line).
            lineVisuals_.push_back(LineVisual{
                a.position,
                b.position,
                kLineCoreWidth * taper,
                0.85F * fade
            });
        }
    }

    void CursorTrailApp::ClearTrail()
    {
        particles_.clear();
        trailPoints_.clear();
        visuals_.clear();
        lineVisuals_.clear();
        renderer_.Hide();
        StopAnimationTimer();
    }

    bool CursorTrailApp::IsTrailAllowed() const
    {
        if (!hideOnNonArrowCursor_)
        {
            return true;
        }

        CURSORINFO cursorInfo{};
        cursorInfo.cbSize = sizeof(cursorInfo);
        if (!GetCursorInfo(&cursorInfo))
        {
            return true;
        }

        if ((cursorInfo.flags & CURSOR_SHOWING) == 0)
        {
            return false;
        }

        return cursorInfo.hCursor == arrowCursor_;
    }

    void CursorTrailApp::ShowTrayMenu()
    {
        HMENU menu = CreatePopupMenu();
        HMENU themeMenu = CreatePopupMenu();
        HMENU styleMenu = CreatePopupMenu();
        HMENU performanceMenu = CreatePopupMenu();
        if (menu == nullptr || themeMenu == nullptr || styleMenu == nullptr || performanceMenu == nullptr)
        {
            if (themeMenu != nullptr) DestroyMenu(themeMenu);
            if (styleMenu != nullptr) DestroyMenu(styleMenu);
            if (performanceMenu != nullptr) DestroyMenu(performanceMenu);
            if (menu != nullptr) DestroyMenu(menu);
            return;
        }

        AppendMenuW(menu, MF_STRING, kCommandPause, paused_ ? L"Resume trail" : L"Pause trail");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

        AppendCheckedMenuItem(styleMenu, kCommandStyleLine, L"Line", trailStyle_ == TrailStyle::Line);
        AppendCheckedMenuItem(styleMenu, kCommandStyleBat, L"Bat", trailStyle_ == TrailStyle::Bat);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(styleMenu), L"Trail style");

        AppendCheckedMenuItem(themeMenu, kCommandThemeRed, L"Ruby", theme_ == Theme::MikuRed);
        AppendCheckedMenuItem(themeMenu, kCommandThemeAqua, L"Aqua", theme_ == Theme::Aqua);
        AppendCheckedMenuItem(themeMenu, kCommandThemeViolet, L"Violet", theme_ == Theme::Violet);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(themeMenu), L"Glow color");

        AppendCheckedMenuItem(performanceMenu, kCommandPerformanceEco, L"Eco (24 FPS)", performanceMode_ == PerformanceMode::Eco);
        AppendCheckedMenuItem(performanceMenu, kCommandPerformanceBalanced, L"Balanced (30 FPS)", performanceMode_ == PerformanceMode::Balanced);
        AppendCheckedMenuItem(performanceMenu, kCommandPerformanceSmooth, L"Smooth (60 FPS)", performanceMode_ == PerformanceMode::Smooth);
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(performanceMenu), L"Performance");

        AppendCheckedMenuItem(menu, kCommandHideOnNonArrow, L"Hide on non-arrow cursor", hideOnNonArrowCursor_);
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(window_);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY,
            cursor.x,
            cursor.y,
            window_,
            nullptr);
        PostMessageW(window_, WM_NULL, 0, 0);

        if (command != 0)
        {
            ExecuteMenuCommand(command);
        }

        DestroyMenu(menu);
    }

    void CursorTrailApp::ExecuteMenuCommand(const UINT command)
    {
        switch (command)
        {
        case kCommandPause:
            TogglePause();
            break;
        case kCommandThemeRed:
            SetTheme(Theme::MikuRed);
            break;
        case kCommandThemeAqua:
            SetTheme(Theme::Aqua);
            break;
        case kCommandThemeViolet:
            SetTheme(Theme::Violet);
            break;
        case kCommandPerformanceEco:
            SetPerformanceMode(PerformanceMode::Eco);
            break;
        case kCommandPerformanceBalanced:
            SetPerformanceMode(PerformanceMode::Balanced);
            break;
        case kCommandPerformanceSmooth:
            SetPerformanceMode(PerformanceMode::Smooth);
            break;
        case kCommandStyleBat:
            SetTrailStyle(TrailStyle::Bat);
            break;
        case kCommandStyleLine:
            SetTrailStyle(TrailStyle::Line);
            break;
        case kCommandHideOnNonArrow:
            hideOnNonArrowCursor_ = !hideOnNonArrowCursor_;
            ClearTrail();
            break;
        case kCommandExit:
            PostMessageW(window_, WM_CLOSE, 0, 0);
            break;
        default:
            break;
        }
    }

    void CursorTrailApp::SetTheme(const Theme theme)
    {
        theme_ = theme;
        switch (theme_)
        {
        case Theme::MikuRed:
            renderer_.SetGlowColor(Gdiplus::Color(255, 255, 0, 0));
            break;
        case Theme::Aqua:
            renderer_.SetGlowColor(Gdiplus::Color(255, 0, 220, 235));
            break;
        case Theme::Violet:
            renderer_.SetGlowColor(Gdiplus::Color(255, 185, 75, 255));
            break;
        }

        ClearTrail();
        RefreshTrayIcon();
    }

    void CursorTrailApp::SetPerformanceMode(const PerformanceMode mode)
    {
        performanceMode_ = mode;
        switch (performanceMode_)
        {
        case PerformanceMode::Eco:
            animationIntervalMilliseconds_ = 42;
            break;
        case PerformanceMode::Balanced:
            animationIntervalMilliseconds_ = 33;
            break;
        case PerformanceMode::Smooth:
            animationIntervalMilliseconds_ = 16;
            break;
        }

        if (animationTimerRunning_ && window_ != nullptr)
        {
            SetTimer(window_, kAnimationTimerId, animationIntervalMilliseconds_, nullptr);
        }
    }

    void CursorTrailApp::SetTrailStyle(const TrailStyle style)
    {
        if (trailStyle_ == style)
        {
            return;
        }

        trailStyle_ = style;
        ClearTrail();
    }

    void CursorTrailApp::TogglePause()
    {
        paused_ = !paused_;
        ClearTrail();
    }

    bool CursorTrailApp::AddTrayIcon()
    {
        if (trayIconAdded_)
        {
            return true;
        }

        RefreshTrayIcon();
        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kTrayCallbackMessage;
        data.hIcon = trayIcon_;
        wcscpy_s(data.szTip, kWindowTitle);

        trayIconAdded_ = Shell_NotifyIconW(NIM_ADD, &data) != FALSE;
        if (trayIconAdded_)
        {
            data.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &data);
        }

        return trayIconAdded_;
    }

    void CursorTrailApp::RemoveTrayIcon()
    {
        if (!trayIconAdded_ || window_ == nullptr)
        {
            return;
        }

        NOTIFYICONDATAW data{};
        data.cbSize = sizeof(data);
        data.hWnd = window_;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        trayIconAdded_ = false;
    }

    void CursorTrailApp::RefreshTrayIcon()
    {
        HICON newIcon = renderer_.CreateTrayIcon();
        if (newIcon == nullptr)
        {
            newIcon = CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
        }

        HICON previousIcon = trayIcon_;
        trayIcon_ = newIcon;

        if (trayIconAdded_)
        {
            NOTIFYICONDATAW data{};
            data.cbSize = sizeof(data);
            data.hWnd = window_;
            data.uID = kTrayIconId;
            data.uFlags = NIF_ICON;
            data.hIcon = trayIcon_;
            Shell_NotifyIconW(NIM_MODIFY, &data);
        }

        if (previousIcon != nullptr)
        {
            DestroyIcon(previousIcon);
        }
    }

    double CursorTrailApp::ElapsedSeconds() const
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - startedAt_).count();
    }

    float CursorTrailApp::RandomBetween(const float minimum, const float maximum)
    {
        return std::uniform_real_distribution<float>(minimum, maximum)(random_);
    }
}
