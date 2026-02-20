#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

std::wstring GetDeviceId();
std::string GetDeviceIdUtf8();
std::string GetDeviceNameUtf8();
std::string GetIPAddressUtf8();
