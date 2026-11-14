// Flashes the app's taskbar button so the user notices Claude finished while
// the window is in the background. Kept separate so <windows.h> stays out of JUCE code.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

void flashTaskbarButton (void* nativeWindowHandle)
{
    auto hwnd = static_cast<HWND> (nativeWindowHandle);

    if (hwnd == nullptr || GetForegroundWindow() == GetAncestor (hwnd, GA_ROOT))
        return;

    FLASHWINFO info {};
    info.cbSize = sizeof (info);
    info.hwnd = GetAncestor (hwnd, GA_ROOT);
    info.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;   // flash until the window is brought to the front
    FlashWindowEx (&info);
}

// Writes a minidump of this process for diagnosing crashes and freezes (Diagnostics.cpp).
#include <dbghelp.h>
#pragma comment (lib, "dbghelp.lib")

bool writeMiniDumpW (const wchar_t* path, void* exceptionPointers, bool fromOtherThread)
{
    HANDLE file = CreateFileW (path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;

    MINIDUMP_EXCEPTION_INFORMATION info {};
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = static_cast<EXCEPTION_POINTERS*> (exceptionPointers);
    info.ClientPointers = FALSE;

    // Thread stacks plus the data they point at: enough to see what every thread was doing.
    const auto type = (MINIDUMP_TYPE) (MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
    const BOOL ok = MiniDumpWriteDump (GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                       exceptionPointers != nullptr ? &info : nullptr, nullptr, nullptr);
    (void) fromOtherThread;
    CloseHandle (file);
    return ok == TRUE;
}
