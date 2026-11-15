#include "logging.h"
#include "skip_intro.h"

#include <windows.h>

#include <mutex>

namespace
{
    std::once_flag g_initializeOnce;

    void InitializeImpl()
    {
        LogInit();
        InstallSkipIntro();
    }
}

// Called by the dinput8 loader before the game's entry point.
extern "C" __declspec(dllexport)
void __cdecl Initialize()
{
    std::call_once(g_initializeOnce, InitializeImpl);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        SetSelfModule(module);
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}
