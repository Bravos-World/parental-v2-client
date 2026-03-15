#include "command_handler.h"
#include "overlay.h"
#include "tray.h"
#include "logger.h"
#include "json.hpp"
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <shellapi.h>

using json = nlohmann::json;

static std::wstring g_unlockPin;
static void(*g_sendFunc)(const std::string& msg) = nullptr;
static std::atomic<bool> g_pendingAction{ false };
static std::thread g_delayThread;
static std::atomic<bool> g_delayCancelled{ false };

static void ExecuteCommand(const std::string& command);

static std::wstring Utf8ToWide(const std::string& s) {
	if (s.empty()) return L"";
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
	return w;
}

static std::string WideToUtf8(const std::wstring& w) {
	if (w.empty()) return "";
	int len = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
	std::string s(len, '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], len, nullptr, nullptr);
	return s;
}

static std::string ToUpperAscii(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return value;
}

static bool ExecutePowerAction(bool restart) {
	LPCWSTR args = restart ? L"/r /t 0 /f" : L"/s /t 0 /f";
	HINSTANCE result = ShellExecuteW(nullptr, L"open", L"shutdown.exe", args, nullptr, SW_HIDE);
	return reinterpret_cast<INT_PTR>(result) > 32;
}

static void SendStatus(const std::string& lockStatus) {
	if (!g_sendFunc) return;
	json j;
	j["type"] = "status";
	j["lockStatus"] = lockStatus;
	g_sendFunc(j.dump());
}

static void SendEvent(const std::string& eventType, const std::string& description = "") {
	if (!g_sendFunc) return;
	json j;
	j["type"] = "event";
	j["eventType"] = eventType;
	if (!description.empty()) j["description"] = description;
	g_sendFunc(j.dump());
}

static void OnPinEntered(const std::wstring& pin) {
	if (pin == g_unlockPin) {
		LogInfo("Correct PIN entered, unlocking");
		g_delayCancelled.store(true);
		OverlayHide();
		SendStatus("UNLOCKED");
		SendEvent("UNLOCK", "Unlocked via PIN");
		TraySetStatusText(L"Status: Unlocked");
	}
	else {
		LogWarning("Incorrect PIN entered");
	}
}

static void OnOverlayAction(const std::wstring& action) {
    const std::string normalizedAction = ToUpperAscii(WideToUtf8(action));
	if (normalizedAction == "SHUTDOWN") {
		LogInfo("Overlay action: SHUTDOWN");
		ExecuteCommand("SHUTDOWN");
	}
	else if (normalizedAction == "RESTART") {
		LogInfo("Overlay action: RESTART");
		ExecuteCommand("RESTART");
	}
	else if (normalizedAction == "LOCK") {
		LogInfo("Overlay action: LOCK");
		ExecuteCommand("LOCK");
	}
	else if (normalizedAction == "UNLOCK") {
		LogInfo("Overlay action: UNLOCK");
		ExecuteCommand("UNLOCK");
	}
}

static void ExecuteCommand(const std::string& command) {
	const std::string normalizedCommand = ToUpperAscii(command);

	if (normalizedCommand == "LOCK") {
		OverlayShow(L"Computer Locked");
		SendStatus("LOCKED");
		SendEvent("LOCK");
		TraySetStatusText(L"Status: Locked");
	}
	else if (normalizedCommand == "UNLOCK") {
		g_delayCancelled.store(true);
		OverlayHide();
		SendStatus("UNLOCKED");
		SendEvent("UNLOCK");
		TraySetStatusText(L"Status: Unlocked");
	}
	else if (normalizedCommand == "SHUTDOWN" || normalizedCommand == "POWEROFF") {
		SendEvent("SHUTDOWN");
		if (!ExecutePowerAction(false)) {
			LogError("Failed to execute SHUTDOWN command");
		}
	}
	else if (normalizedCommand == "RESTART" || normalizedCommand == "REBOOT") {
		SendEvent("RESTART");
		if (!ExecutePowerAction(true)) {
			LogError("Failed to execute RESTART command");
		}
	}
}

static void DelayedExecute(const std::string& command, int delaySec) {
	std::wstring cmdLabel = Utf8ToWide(command);
	std::wstring notice = L"You will be " + cmdLabel + L" after " + std::to_wstring(delaySec) + L"s";
	MessageBoxW(nullptr, notice.c_str(), L"Scheduled Action", MB_OK | MB_ICONINFORMATION);

	g_delayCancelled.store(false);
	for (int i = 0; i < delaySec * 10; ++i) {
		if (g_delayCancelled.load()) return;
		Sleep(100);
	}

 if (!g_delayCancelled.load()) {
		std::wstring* action = new std::wstring(Utf8ToWide(ToUpperAscii(command)));
		HWND overlayHwnd = OverlayGetHwnd();
		if (!overlayHwnd || !PostMessageW(overlayHwnd, WM_APP + 101, (WPARAM)action, 0)) {
			delete action;
			LogError("Failed to post scheduled action to UI thread");
		}
	}
}

void CmdInit(const std::wstring& unlockPin) {
	g_unlockPin = unlockPin;
	OverlaySetPinCallback(OnPinEntered);
	OverlaySetActionCallback(OnOverlayAction);
}

void CmdSetSendFunc(void(*sendFunc)(const std::string& msg)) {
	g_sendFunc = sendFunc;
}

void CmdHandleMessage(const std::string& jsonMessage) {
	json j;
	try {
		j = json::parse(jsonMessage);
	}
	catch (...) {
		return;
	}

	std::string type = j.value("type", "");

	if (type == "registered") {
		TraySetStatusText(L"Status: Connected");
		SendEvent("CONNECT");
		return;
	}

	if (type == "command") {
		std::string command = j.value("command", "");
		int delaySec = j.value("delaySeconds", 0);

		if (g_delayThread.joinable()) {
			g_delayCancelled.store(true);
			g_delayThread.join();
		}

		if (delaySec <= 0) {
			ExecuteCommand(command);
		}
		else {
			g_delayThread = std::thread(DelayedExecute, command, delaySec);
		}
		return;
	}

	if (type == "message") {
		std::string content = j.value("content", "");
		std::wstring wContent = Utf8ToWide(content);
		if (OverlayIsVisible())
			OverlayShowMessage(L"Message", wContent);
		else
			MessageBoxW(nullptr, wContent.c_str(), L"Message", MB_OK | MB_ICONINFORMATION);
		return;
	}
}
