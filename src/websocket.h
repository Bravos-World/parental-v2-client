#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <string>
#include <functional>

using WsMessageCallback = std::function<void(const std::string& message)>;
using WsDisconnectCallback = std::function<void()>;
using WsConnectCallback = std::function<void()>;

void WsInit(const std::wstring& scheme, const std::wstring& host, int port, const std::wstring& secretKey = L"");
void WsSetOnMessage(WsMessageCallback cb);
void WsSetOnDisconnect(WsDisconnectCallback cb);
void WsSetOnConnect(WsConnectCallback cb);
bool WsConnect();
void WsDisconnect();
bool WsIsConnected();
void WsSend(const std::string& message);
void WsPoll();
void WsStartReconnectThread();
void WsStopReconnectThread();
