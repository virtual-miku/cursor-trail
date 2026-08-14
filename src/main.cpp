#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#include "CursorTrailApp.h"

namespace
{
    void EnablePerMonitorDpiAwareness()
    {
        using SetDpiAwarenessContext = BOOL(WINAPI*)(HANDLE);
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto setContext = reinterpret_cast<SetDpiAwarenessContext>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));

        if (setContext != nullptr)
        {
            setContext(reinterpret_cast<HANDLE>(-4));
        }
        else
        {
            SetProcessDPIAware();
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\CursorTrail.Native.Singleton");
    if (singleton == nullptr)
    {
        return 1;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        CloseHandle(singleton);
        return 0;
    }

    EnablePerMonitorDpiAwareness();

    cursor_trail::CursorTrailApp app;
    if (!app.Initialize(instance))
    {
        MessageBoxW(
            nullptr,
            L"Cursor Trail gagal dimulai. Coba build ulang atau periksa Windows Event Viewer.",
            L"Cursor Trail",
            MB_OK | MB_ICONERROR);
        CloseHandle(singleton);
        return 1;
    }

    const int exitCode = app.Run();
    CloseHandle(singleton);
    return exitCode;
}
