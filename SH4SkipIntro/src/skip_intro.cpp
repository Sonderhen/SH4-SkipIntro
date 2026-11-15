#include "skip_intro.h"

#include "logging.h"

#include <windows.h>

#include <cstdint>
#include <cstring>

namespace
{
    // SH4 code (RVA = VA - 0x400000).
    constexpr uintptr_t kCallSiteRva     = 0x0002CD6A; // 0042CD6A: call 00519160 (logos task)
    constexpr uintptr_t kLogosTaskRva    = 0x00119160; // 00519160
    constexpr uintptr_t kStepPopRva      = 0x00155C30; // 00555C30: leave a step level
    constexpr uintptr_t kStepPushRva     = 0x00155C20; // 00555C20: enter a step level
    constexpr uintptr_t kStepSetRva      = 0x00155C50; // 00555C50(step): set the current level's step
    constexpr uintptr_t kMemFreeRva      = 0x00152050; // 00552050(block)
    constexpr uintptr_t kGammaRva        = 0x001212E0; // 005212E0(value)
    constexpr uintptr_t kFadeRva         = 0x00165CA0; // 00565CA0(level), 0x80 = black
    constexpr uintptr_t kEnterMenuRva    = 0x00127A50; // 00527A50(kind) before mode 3

    // SH4 data.
    constexpr uintptr_t kStepDepthRva    = 0x00E36D48; // 01236D48
    constexpr uintptr_t kStepTableRva    = 0x00E36D4C; // 01236D4C + depth * 4
    constexpr uintptr_t kLogosMemoryRva  = 0x00B9D900; // 00F9D900 (block of mode 1)
    constexpr uintptr_t kGammaValueRva   = 0x00C91AEC; // 01091AEC
    constexpr uintptr_t kInputManagerRva = 0x002E8EA4; // 006E8EA4 (pointer)
    constexpr uintptr_t kPressedOffset   = 0x34;
    constexpr uint32_t  kSkipDemoBit     = 0x200;      // order of 005FF3D0: ... Evade, SkipDemo
    constexpr uint32_t  kLogosOnScreen   = 2;          // logos task step: logos running
    constexpr uint32_t  kMenuMode        = 3;          // top-level mode 3: main menu
    constexpr int       kMenuKind        = 2;          // as after the opening movie
    constexpr int       kFadeBlack       = 0x80;

    using VoidFn = void(__cdecl*)();
    using IntArgFn = void(__cdecl*)(int);

    uintptr_t g_base = 0;
    uintptr_t g_previous = 0; // target of the call before the hook
    bool g_installed = false;
    bool g_skipped = false;

    template <typename T>
    T Fn(uintptr_t rva)
    {
        return reinterpret_cast<T>(g_base + rva);
    }

    uint32_t U32(uintptr_t rva)
    {
        return *reinterpret_cast<volatile const uint32_t*>(g_base + rva);
    }

    bool SkipPressed()
    {
        const uint32_t manager = U32(kInputManagerRva);
        if (!manager)
            return false;
        const uint32_t pressed = *reinterpret_cast<volatile const uint32_t*>(manager + kPressedOffset);
        return (pressed & kSkipDemoBit) != 0;
    }

    // Step of the logos task (the level below mode 1's own level).
    uint32_t LogosStep()
    {
        const uint32_t depth = U32(kStepDepthRva);
        return U32(kStepTableRva + (depth + 1) * 4);
    }

    // SH4's own end of the opening, done from inside mode 1's step level:
    // out of it, mode 3 set at the top level, back in it (SH4 leaves it
    // right after this call).
    void GoToMenu()
    {
        const uint32_t block = U32(kLogosMemoryRva);
        Fn<IntArgFn>(kMemFreeRva)(static_cast<int>(block));
        *reinterpret_cast<volatile uint32_t*>(g_base + kLogosMemoryRva) = 0;

        Fn<IntArgFn>(kGammaRva)(static_cast<int>(U32(kGammaValueRva)));
        Fn<IntArgFn>(kFadeRva)(kFadeBlack);

        Fn<VoidFn>(kStepPopRva)();
        Fn<IntArgFn>(kStepSetRva)(static_cast<int>(kMenuMode));
        Fn<IntArgFn>(kEnterMenuRva)(kMenuKind);
        Fn<VoidFn>(kStepPushRva)();
    }

    void __cdecl LogosHook()
    {
        bool skip = false;
        __try
        {
            skip = !g_skipped && LogosStep() == kLogosOnScreen && SkipPressed();
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            skip = false;
        }

        if (!skip)
        {
            reinterpret_cast<VoidFn>(g_previous)();
            return;
        }

        g_skipped = true;
        Log("SH4SkipIntro: SkipDemo pressed during the logos -> main menu");
        GoToMenu();
    }

    bool PatchCall(uintptr_t site, const void* target)
    {
        uint8_t code[5];
        code[0] = 0xE8;
        const int32_t rel = static_cast<int32_t>(reinterpret_cast<uintptr_t>(target) - (site + 5));
        std::memcpy(code + 1, &rel, 4);

        DWORD old = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(site), 5, PAGE_EXECUTE_READWRITE, &old))
            return false;
        std::memcpy(reinterpret_cast<void*>(site), code, 5);
        DWORD tmp = 0;
        VirtualProtect(reinterpret_cast<void*>(site), 5, old, &tmp);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(site), 5);
        return true;
    }
}

bool InstallSkipIntro()
{
    if (g_installed)
        return true;

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!g_base)
        return false;

    const uintptr_t site = g_base + kCallSiteRva;
    const uint8_t* code = reinterpret_cast<const uint8_t*>(site);
    if (code[0] != 0xE8)
    {
        Log("SH4SkipIntro: 0042CD6A is not a call; not installed");
        return false;
    }

    int32_t rel = 0;
    std::memcpy(&rel, code + 1, 4);
    g_previous = site + 5 + rel;

    // Another mod may already be chained here; its target is called instead.
    if (g_previous != g_base + kLogosTaskRva)
        Log("SH4SkipIntro: 0042CD6A already redirected to %08X; chained", static_cast<unsigned>(g_previous));

    if (!PatchCall(site, reinterpret_cast<const void*>(&LogosHook)))
    {
        Log("SH4SkipIntro: could not patch 0042CD6A; not installed");
        return false;
    }

    g_installed = true;
    Log("SH4SkipIntro: installed | SkipDemo during the start-up logos -> main menu");
    return true;
}
