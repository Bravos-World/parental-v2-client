#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include "device_id.h"
#include <sstream>
#include <iomanip>

std::wstring GetDeviceId() {
    DWORD serialNumber = 0;
    GetVolumeInformationW(L"C:\\", nullptr, 0, &serialNumber, nullptr, nullptr, nullptr, 0);

    WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &nameLen);

    std::wstringstream ss;
    ss << std::hex << std::uppercase << std::setfill(L'0') << std::setw(8) << serialNumber;
    ss << L"-";
    ss << computerName;
    return ss.str();
}

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
    return s;
}

std::string GetDeviceIdUtf8() {
    return WideToUtf8(GetDeviceId());
}

std::string GetDeviceNameUtf8() {
    WCHAR computerName[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD nameLen = MAX_COMPUTERNAME_LENGTH + 1;
    GetComputerNameW(computerName, &nameLen);
    return WideToUtf8(computerName);
}

std::string GetIPAddressUtf8() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) return "0.0.0.0";

    char hostname[256] = {};
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        WSACleanup();
        return "0.0.0.0";
    }

    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0 || !result) {
        WSACleanup();
        return "0.0.0.0";
    }

    char ipStr[INET_ADDRSTRLEN] = {};
    sockaddr_in* addr = (sockaddr_in*)result->ai_addr;
    inet_ntop(AF_INET, &addr->sin_addr, ipStr, sizeof(ipStr));
    freeaddrinfo(result);

    return std::string(ipStr);
}
