#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

enum class LogLevel {
    INFO,
    WARNING,
    LOG_ERROR
};

void LogInit();
void LogShutdown();
void LogInfo(const std::string& message);
void LogWarning(const std::string& message);
void LogError(const std::string& message);
void LogInfoW(const std::wstring& message);
void LogWarningW(const std::wstring& message);
void LogErrorW(const std::wstring& message);
