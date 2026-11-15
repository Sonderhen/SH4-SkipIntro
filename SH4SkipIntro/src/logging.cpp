#include "logging.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>

namespace
{
    constexpr const wchar_t* kSharedLogFile =
        L"SH4CustomModules.log";

    constexpr const char* kModuleTag =
        "SH4SkipIntro";

    HMODULE g_selfModule = nullptr;
    std::mutex g_localMutex;
    std::wstring g_logPath;

    HANDLE g_crossModuleMutex = nullptr;
    HANDLE g_sessionMarker = nullptr;
    bool g_moduleInitWritten = false;

    std::wstring DirName(std::wstring path)
    {
        const auto pos = path.find_last_of(L"\\/");
        if (pos == std::wstring::npos)
            return L".";
        return path.substr(0, pos);
    }

    std::wstring MakeKernelObjectName(
        const wchar_t* prefix)
    {
        wchar_t name[128]{};

        _snwprintf_s(
            name,
            _countof(name),
            _TRUNCATE,
            L"Local\\%ls_%lu",
            prefix,
            static_cast<unsigned long>(
                GetCurrentProcessId()));

        return name;
    }

    void EnsureCrossModuleMutexLocked()
    {
        if (g_crossModuleMutex)
            return;

        const std::wstring mutexName =
            MakeKernelObjectName(
                L"SH4CustomModulesLogMutex");

        g_crossModuleMutex =
            CreateMutexW(
                nullptr,
                FALSE,
                mutexName.c_str());
    }

    bool LockCrossModule()
    {
        if (!g_crossModuleMutex)
            return false;

        const DWORD wait =
            WaitForSingleObject(
                g_crossModuleMutex,
                2000);

        return
            wait == WAIT_OBJECT_0 ||
            wait == WAIT_ABANDONED;
    }

    void UnlockCrossModule(bool locked)
    {
        if (locked && g_crossModuleMutex)
            ReleaseMutex(g_crossModuleMutex);
    }

    void WriteFormattedLine(
        FILE* file,
        const char* module,
        const char* message)
    {
        if (!file)
            return;

        SYSTEMTIME st{};
        GetLocalTime(&st);

        fprintf(
            file,
            "[%02u:%02u:%02u.%03u][%s] %s\n",
            st.wHour,
            st.wMinute,
            st.wSecond,
            st.wMilliseconds,
            module,
            message ? message : "");
    }

    // local mutex must already be held.
    void EnsureLogSessionLocked()
    {
        if (g_logPath.empty())
        {
            // Use the game executable directory rather than each DLL's own
            // directory. This guarantees both modules target the same log even
            // if a loader places one DLL in a subfolder.
            wchar_t path[MAX_PATH]{};
            GetModuleFileNameW(
                nullptr,
                path,
                MAX_PATH);

            g_logPath =
                DirName(path) +
                L"\\" +
                kSharedLogFile;
        }

        EnsureCrossModuleMutexLocked();

        const bool crossLocked =
            LockCrossModule();

        // The session marker is created only while holding the named mutex.
        // Therefore the first DLL finishes truncating/creating the new log
        // before the second DLL can append its initialization line.
        bool firstLoggerThisProcess = false;

        if (!g_sessionMarker)
        {
            const std::wstring markerName =
                MakeKernelObjectName(
                    L"SH4CustomModulesLogSession");

            SetLastError(
                ERROR_SUCCESS);

            g_sessionMarker =
                CreateFileMappingW(
                    INVALID_HANDLE_VALUE,
                    nullptr,
                    PAGE_READWRITE,
                    0,
                    1,
                    markerName.c_str());

            if (g_sessionMarker)
            {
                firstLoggerThisProcess =
                    GetLastError() !=
                        ERROR_ALREADY_EXISTS;
            }
        }

        if (firstLoggerThisProcess)
        {
            FILE* file = nullptr;
            _wfopen_s(&file, g_logPath.c_str(), L"w");

            if (file)
            {
                char initMessage[160]{};
                _snprintf_s(
                    initMessage,
                    sizeof(initMessage),
                    _TRUNCATE,
                    "DLL log initialized | PID=%lu | first module in this SH4 session",
                    static_cast<unsigned long>(
                        GetCurrentProcessId()));

                WriteFormattedLine(
                    file,
                    kModuleTag,
                    initMessage);

                fclose(file);
                g_moduleInitWritten = true;
            }
        }

        if (!g_moduleInitWritten)
        {
            FILE* file = nullptr;
            _wfopen_s(&file, g_logPath.c_str(), L"a");

            if (file)
            {
                WriteFormattedLine(
                    file,
                    kModuleTag,
                    "DLL log initialized");

                fclose(file);
            }

            g_moduleInitWritten = true;
        }

        UnlockCrossModule(crossLocked);
    }
}

void SetSelfModule(HMODULE module)
{
    g_selfModule = module;
}

std::wstring GetDllDirectory()
{
    wchar_t path[MAX_PATH]{};

    if (g_selfModule)
        GetModuleFileNameW(g_selfModule, path, MAX_PATH);
    else
        GetModuleFileNameW(nullptr, path, MAX_PATH);

    return DirName(path);
}

void LogInit()
{
    std::lock_guard<std::mutex> lock(g_localMutex);
    EnsureLogSessionLocked();
}

void Log(const char* fmt, ...)
{
    char buffer[2048]{};

    va_list ap;
    va_start(ap, fmt);
    _vsnprintf_s(
        buffer,
        sizeof(buffer),
        _TRUNCATE,
        fmt,
        ap);
    va_end(ap);

    std::lock_guard<std::mutex> lock(g_localMutex);

    if (g_logPath.empty() ||
        !g_sessionMarker ||
        !g_moduleInitWritten)
    {
        EnsureLogSessionLocked();
    }

    const bool crossLocked =
        LockCrossModule();

    FILE* file = nullptr;
    _wfopen_s(&file, g_logPath.c_str(), L"a");

    if (file)
    {
        WriteFormattedLine(
            file,
            kModuleTag,
            buffer);

        fclose(file);
    }

    UnlockCrossModule(crossLocked);
}

