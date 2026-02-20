#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#include "config.h"
#include "device_id.h"
#include "websocket.h"
#include "command_handler.h"
#include "overlay.h"
#include "tray.h"
#include "logger.h"
#include "json.hpp"

using json = nlohmann::json;

static AppConfig g_config;
static HINSTANCE g_hInst;
static HWND g_hMsgWnd = nullptr;

static void WsSendWrapper(const std::string& msg) {
    WsSend(msg);
}

static void SendRegister() {
    json reg;
    reg["type"] = "register";
    reg["deviceId"] = GetDeviceIdUtf8();
    reg["deviceName"] = GetDeviceNameUtf8();
    reg["ipAddress"] = GetIPAddressUtf8();
    std::string regMsg = reg.dump();
    LogInfo("Sending register: " + regMsg);
    WsSend(regMsg);
}

static void OnMessage(const std::string& msg) {
    if (g_hMsgWnd) {
        std::string* pMsg = new std::string(msg);
        PostMessageW(g_hMsgWnd, WM_USER + 200, (WPARAM)pMsg, 0);
    }
}

static void OnDisconnect() {
	LogWarning("WebSocket disconnected");
	TraySetStatusText(L"Status: Disconnected");
}

static void OnConnect() {
	LogInfo("WebSocket connected, sending register");
	SendRegister();
}

static LRESULT CALLBACK MsgRouterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
	HANDLE hMutex = CreateMutexW(nullptr, TRUE, L"Global\\ParentClientSingleInstance");
	if (GetLastError() == ERROR_ALREADY_EXISTS) {
		if (hMutex) CloseHandle(hMutex);
		return 0;
	}

	g_hInst = hInstance;

	LogInit();
	LogInfo("Application starting");

	WSADATA wsaData;
	WSAStartup(MAKEWORD(2, 2), &wsaData);

	g_config = LoadConfig();
	LogInfoW(L"Server: " + g_config.serverScheme + L"://" + g_config.serverHost + L":" + std::to_wstring(g_config.serverPort));
	LogInfoW(L"Device ID: " + GetDeviceId());

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = MsgRouterProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"ParentClientMsgRouter";
    RegisterClassExW(&wc);

    HWND hMsgWnd = CreateWindowExW(0, L"ParentClientMsgRouter", L"",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    g_hMsgWnd = hMsgWnd;

    OverlayCreate(hInstance);
    TrayCreate(hInstance, hMsgWnd);
    TraySetPinForExit(g_config.unlockPin);

    CmdInit(g_config.unlockPin);
    CmdSetSendFunc(WsSendWrapper);

    WsInit(g_config.serverScheme, g_config.serverHost, g_config.serverPort, g_config.secretKey);
    WsSetOnMessage(OnMessage);
    WsSetOnDisconnect(OnDisconnect);
    WsSetOnConnect(OnConnect);
    WsStartReconnectThread();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }

    WsStopReconnectThread();
    WsDisconnect();
    OverlayDestroy();
    TrayDestroy();
    WSACleanup();

    LogInfo("Application exiting");
    LogShutdown();

    if (hMutex) {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }

    return 0;
}

static LRESULT CALLBACK MsgRouterProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_USER + 200) {
        std::string* pMsg = (std::string*)wParam;
        if (pMsg) {
            LogInfo("Received message: " + *pMsg);
            CmdHandleMessage(*pMsg);
            delete pMsg;
        }
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}
