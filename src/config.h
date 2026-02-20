#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

struct AppConfig {
    std::wstring serverScheme;
    std::wstring serverHost;
    int serverPort = 443;
    std::wstring unlockPin;
	std::wstring secretKey;
};

AppConfig LoadConfig();
