#pragma once

#include <windows.h>
#include <string>

void SetSelfModule(HMODULE module);
std::wstring GetDllDirectory();
void LogInit();
void Log(const char* fmt, ...);
