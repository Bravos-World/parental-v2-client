#include "config.h"
#include "logger.h"

static std::wstring GetExeDir() {
    WCHAR path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s(path);
    size_t pos = s.find_last_of(L"\\/");
    if (pos != std::wstring::npos) s = s.substr(0, pos);
    return s;
}

static std::wstring Trim(const std::wstring& s) {
    size_t start = s.find_first_not_of(L" \t\r\n");
    if (start == std::wstring::npos) return L"";
    size_t end = s.find_last_not_of(L" \t\r\n");
    return s.substr(start, end - start + 1);
}

AppConfig LoadConfig() {
    AppConfig cfg;
    cfg.serverScheme = L"wss";
    cfg.serverHost = L"localhost";
    cfg.serverPort = 443;
    cfg.unlockPin = L"1234";
    cfg.secretKey = L"";

    std::wstring iniPath = GetExeDir() + L"\\config.ini";
    HANDLE hFile = CreateFileW(iniPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        LogWarningW(L"Config file not found, using defaults: " + iniPath);
        return cfg;
    }

    DWORD fileSize = GetFileSize(hFile, nullptr);
    if (fileSize == 0 || fileSize == INVALID_FILE_SIZE) {
        CloseHandle(hFile);
        return cfg;
    }

    char* buf = new char[fileSize + 1];
    DWORD bytesRead = 0;
    ReadFile(hFile, buf, fileSize, &bytesRead, nullptr);
    CloseHandle(hFile);
    buf[bytesRead] = '\0';

    int wLen = MultiByteToWideChar(CP_UTF8, 0, buf, (int)bytesRead, nullptr, 0);
    std::wstring content(wLen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, buf, (int)bytesRead, &content[0], wLen);
    delete[] buf;

    size_t pos = 0;
    while (pos < content.size()) {
        size_t eol = content.find(L'\n', pos);
        if (eol == std::wstring::npos) eol = content.size();
        std::wstring line = Trim(content.substr(pos, eol - pos));
        pos = eol + 1;

        if (line.empty() || line[0] == L'#' || line[0] == L';') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos) continue;

        std::wstring key = Trim(line.substr(0, eq));
        std::wstring val = Trim(line.substr(eq + 1));

        if (key == L"SERVER_SCHEME") cfg.serverScheme = val;
        else if (key == L"SERVER_HOST") cfg.serverHost = val;
        else if (key == L"SERVER_PORT") cfg.serverPort = _wtoi(val.c_str());
        else if (key == L"UNLOCK_PIN") cfg.unlockPin = val;
		else if (key == L"SECRET_KEY") cfg.secretKey = val;
    }

    return cfg;
}
