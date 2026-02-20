#include "logger.h"
#include <fstream>
#include <mutex>
#include <ctime>
#include <iomanip>
#include <sstream>

static std::ofstream g_logFile;
static std::mutex g_logMutex;
static bool g_logInitialized = false;

static std::wstring GetExeDir() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    size_t pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s = s.substr(0, pos);
    return s;
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

static std::string GetTimestamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(4) << st.wYear << "-"
        << std::setw(2) << st.wMonth << "-"
        << std::setw(2) << st.wDay << " "
        << std::setw(2) << st.wHour << ":"
        << std::setw(2) << st.wMinute << ":"
        << std::setw(2) << st.wSecond << "."
        << std::setw(3) << st.wMilliseconds;
    return oss.str();
}

static const char* LogLevelToString(LogLevel level) {
    switch (level) {
    case LogLevel::INFO: return "INFO";
    case LogLevel::WARNING: return "WARN";
    case LogLevel::LOG_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void WriteLog(LogLevel level, const std::string& message) {
    if (!g_logInitialized) return;

    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile << "[" << GetTimestamp() << "] "
            << "[" << LogLevelToString(level) << "] "
            << message << std::endl;
        g_logFile.flush();
    }
}

void LogInit() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logInitialized) return;

    std::wstring logPath = GetExeDir() + L"\\log.txt";

    g_logFile.open(logPath.c_str(), std::ios::out | std::ios::app);
    if (g_logFile.is_open()) {
        g_logInitialized = true;
        g_logFile << "\n========================================\n";
        g_logFile << "ParentClient started at " << GetTimestamp() << "\n";
        g_logFile << "========================================\n";
        g_logFile.flush();
    }
}

void LogShutdown() {
    std::lock_guard<std::mutex> lock(g_logMutex);
    if (g_logFile.is_open()) {
        g_logFile << "========================================\n";
        g_logFile << "ParentClient stopped at " << GetTimestamp() << "\n";
        g_logFile << "========================================\n\n";
        g_logFile.close();
    }
    g_logInitialized = false;
}

void LogInfo(const std::string& message) {
    WriteLog(LogLevel::INFO, message);
}

void LogWarning(const std::string& message) {
    WriteLog(LogLevel::WARNING, message);
}

void LogError(const std::string& message) {
    WriteLog(LogLevel::LOG_ERROR, message);
}

void LogInfoW(const std::wstring& message) {
    WriteLog(LogLevel::INFO, WideToUtf8(message));
}

void LogWarningW(const std::wstring& message) {
    WriteLog(LogLevel::WARNING, WideToUtf8(message));
}

void LogErrorW(const std::wstring& message) {
    WriteLog(LogLevel::LOG_ERROR, WideToUtf8(message));
}
