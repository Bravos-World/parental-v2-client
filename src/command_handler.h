#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

void CmdInit(const std::wstring& unlockPin);
void CmdHandleMessage(const std::string& jsonMessage);
void CmdSetSendFunc(void(*sendFunc)(const std::string& msg));
