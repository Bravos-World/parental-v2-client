#include "websocket.h"
#include "logger.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <sstream>

static HINTERNET g_hSession = nullptr;
static HINTERNET g_hConnect = nullptr;
static HINTERNET g_hRequest = nullptr;
static HINTERNET g_hWebSocket = nullptr;

static std::wstring g_scheme;
static std::wstring g_host;
static int g_port = 443;
static std::wstring g_secretKey;

static WsMessageCallback g_onMessage;
static WsDisconnectCallback g_onDisconnect;
static WsConnectCallback g_onConnect;

static std::atomic<bool> g_connected{false};
static std::atomic<bool> g_reconnectRunning{false};
static std::thread g_recvThread;
static std::thread g_reconnectThread;
static std::thread g_pingThread;
static std::atomic<bool> g_recvRunning{false};
static std::atomic<bool> g_pingRunning{false};
static std::mutex g_sendMutex;

void WsInit(const std::wstring& scheme, const std::wstring& host, int port, const std::wstring& secretKey) {
    g_scheme = scheme;
    g_host = host;
    g_port = port;
    g_secretKey = secretKey;
}

void WsSetOnMessage(WsMessageCallback cb) { g_onMessage = cb; }
void WsSetOnDisconnect(WsDisconnectCallback cb) { g_onDisconnect = cb; }
void WsSetOnConnect(WsConnectCallback cb) { g_onConnect = cb; }
bool WsIsConnected() { return g_connected.load(); }

static void CleanupHandles() {
    if (g_hWebSocket) { WinHttpCloseHandle(g_hWebSocket); g_hWebSocket = nullptr; }
    if (g_hRequest) { WinHttpCloseHandle(g_hRequest); g_hRequest = nullptr; }
    if (g_hConnect) { WinHttpCloseHandle(g_hConnect); g_hConnect = nullptr; }
    if (g_hSession) { WinHttpCloseHandle(g_hSession); g_hSession = nullptr; }
}

static void RecvThreadProc() {
    BYTE buffer[4096];
    while (g_recvRunning.load() && g_connected.load()) {
        DWORD bytesRead = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE bufType;
        std::string accumulated;
        DWORD err;
        do {
            err = WinHttpWebSocketReceive(g_hWebSocket, buffer, sizeof(buffer), &bytesRead, &bufType);
            if (err != ERROR_SUCCESS) {
                g_connected.store(false);
                if (g_onDisconnect) g_onDisconnect();
                return;
            }
            if (bufType == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                g_connected.store(false);
                if (g_onDisconnect) g_onDisconnect();
                return;
            }
            accumulated.append((char*)buffer, bytesRead);
        } while (bufType == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE);

        if (!accumulated.empty() && g_onMessage) {
            g_onMessage(accumulated);
        }
    }
}

static void PingThreadProc() {
    while (g_pingRunning.load() && g_connected.load()) {
        for (int i = 0; i < 300 && g_pingRunning.load() && g_connected.load(); ++i) {
            Sleep(100);
        }
        if (!g_connected.load() || !g_pingRunning.load()) break;
        std::lock_guard<std::mutex> lock(g_sendMutex);
        if (g_hWebSocket && g_connected.load()) {
            std::string ping = "{\"type\":\"ping\"}";
            WinHttpWebSocketSend(g_hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                (PVOID)ping.c_str(), (DWORD)ping.size());
        }
    }
}

bool WsConnect() {
    g_recvRunning.store(false);
    g_pingRunning.store(false);
    g_connected.store(false);

    if (g_recvThread.joinable()) g_recvThread.join();
    if (g_pingThread.joinable()) g_pingThread.join();

    CleanupHandles();

    LogInfo("Attempting WebSocket connection...");

    g_hSession = WinHttpOpen(L"ParentClient/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!g_hSession) {
        LogError("WinHttpOpen failed, error=" + std::to_string(GetLastError()));
        return false;
    }

    g_hConnect = WinHttpConnect(g_hSession, g_host.c_str(), (INTERNET_PORT)g_port, 0);
    if (!g_hConnect) { 
        LogError("WinHttpConnect failed, error=" + std::to_string(GetLastError()));
        CleanupHandles(); 
        return false; 
    }

    DWORD flags = 0;
    if (g_scheme == L"wss" || g_scheme == L"https") {
        flags = WINHTTP_FLAG_SECURE;
    }

    g_hRequest = WinHttpOpenRequest(g_hConnect, L"GET", L"/ws/device",
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!g_hRequest) { 
        LogError("WinHttpOpenRequest failed, error=" + std::to_string(GetLastError()));
        CleanupHandles(); 
        return false; 
    }

    if (!WinHttpSetOption(g_hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        LogError("WinHttpSetOption (upgrade) failed, error=" + std::to_string(GetLastError()));
        CleanupHandles();
        return false;
    }

    if (!g_secretKey.empty()) {
        std::wstring header = L"X-Secret-Key: " + g_secretKey;
        WinHttpAddRequestHeaders(g_hRequest, header.c_str(), (DWORD)header.size(), WINHTTP_ADDREQ_FLAG_ADD);
    }

    BOOL result = WinHttpSendRequest(g_hRequest,
        WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!result) { 
        LogError("WinHttpSendRequest failed, error=" + std::to_string(GetLastError()));
        CleanupHandles(); 
        return false; 
    }

    result = WinHttpReceiveResponse(g_hRequest, nullptr);
    if (!result) { 
        LogError("WinHttpReceiveResponse failed, error=" + std::to_string(GetLastError()));
        CleanupHandles(); 
        return false; 
    }

    g_hWebSocket = WinHttpWebSocketCompleteUpgrade(g_hRequest, 0);
    if (!g_hWebSocket) { 
        LogError("WinHttpWebSocketCompleteUpgrade failed, error=" + std::to_string(GetLastError()));
        CleanupHandles(); 
        return false; 
    }

    WinHttpCloseHandle(g_hRequest);
    g_hRequest = nullptr;

    g_connected.store(true);
    LogInfo("WebSocket connected successfully");

    g_recvRunning.store(true);
    g_recvThread = std::thread(RecvThreadProc);

    g_pingRunning.store(true);
    g_pingThread = std::thread(PingThreadProc);

    if (g_onConnect) g_onConnect();

    return true;
}

void WsDisconnect() {
    g_recvRunning.store(false);
    g_pingRunning.store(false);

    if (g_hWebSocket && g_connected.load()) {
        LogInfo("Closing WebSocket connection");
        WinHttpWebSocketClose(g_hWebSocket, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
    }
    g_connected.store(false);

    if (g_recvThread.joinable()) g_recvThread.join();
    if (g_pingThread.joinable()) g_pingThread.join();

    CleanupHandles();
}

void WsSend(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_sendMutex);
    if (g_hWebSocket && g_connected.load()) {
        WinHttpWebSocketSend(g_hWebSocket, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
            (PVOID)message.c_str(), (DWORD)message.size());
    } else {
        LogWarning("Cannot send: WebSocket not connected");
    }
}

void WsPoll() {
}

static void ReconnectThreadProc() {
    int delay = 2000;
    while (g_reconnectRunning.load()) {
        if (!g_connected.load()) {
            if (WsConnect()) {
                delay = 2000;
            } else {
                LogWarning("Reconnect failed, retrying in " + std::to_string(delay / 1000) + "s");
                for (int i = 0; i < delay / 100 && g_reconnectRunning.load(); ++i) {
                    Sleep(100);
                }
                if (delay < 60000) delay *= 2;
                if (delay > 60000) delay = 60000;
            }
        } else {
            Sleep(500);
        }
    }
}

void WsStartReconnectThread() {
    g_reconnectRunning.store(true);
    g_reconnectThread = std::thread(ReconnectThreadProc);
}

void WsStopReconnectThread() {
    g_reconnectRunning.store(false);
    if (g_reconnectThread.joinable()) g_reconnectThread.join();
}
