#include "command_handler.h"
#include "overlay.h"
#include "tray.h"
#include "logger.h"
#include "json.hpp"
#include <thread>
#include <atomic>

using json = nlohmann::json;

static std::wstring g_unlockPin;
static void(*g_sendFunc)(const std::string& msg) = nullptr;
static std::atomic<bool> g_pendingAction{ false };
static std::thread g_delayThread;
static std::atomic<bool> g_delayCancelled{ false };

static std::wstring Utf8ToWide(const std::string& s) {
	if (s.empty()) return L"";
	int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
	std::wstring w(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
	return w;
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

static void ExecuteCommand(const std::string& command) {
	if (command == "LOCK") {
		OverlayShow(L"Computer Locked");
		SendStatus("LOCKED");
		SendEvent("LOCK");
		TraySetStatusText(L"Status: Locked");
	}
	else if (command == "UNLOCK") {
		g_delayCancelled.store(true);
		OverlayHide();
		SendStatus("UNLOCKED");
		SendEvent("UNLOCK");
		TraySetStatusText(L"Status: Unlocked");
	}
	else if (command == "SHUTDOWN") {
		SendEvent("SHUTDOWN");
		HANDLE hToken;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
			TOKEN_PRIVILEGES tp;
			LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid);
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
			CloseHandle(hToken);
		}
		InitiateSystemShutdownExW(
			nullptr,
			const_cast<LPWSTR>(L"System shutdown initiated by parental control"),
			0,      
			TRUE,     
			FALSE,    
			SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED
		);
	}
	else if (command == "RESTART") {
		SendEvent("RESTART");
		HANDLE hToken;
		if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
			TOKEN_PRIVILEGES tp;
			LookupPrivilegeValueW(nullptr, L"SeShutdownPrivilege", &tp.Privileges[0].Luid);
			tp.PrivilegeCount = 1;
			tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
			AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
			CloseHandle(hToken);
		}
		InitiateSystemShutdownExW(
			nullptr,
			const_cast<LPWSTR>(L"System restart initiated by parental control"),
			0,
			TRUE,
			TRUE,
			SHTDN_REASON_MAJOR_OTHER | SHTDN_REASON_MINOR_OTHER | SHTDN_REASON_FLAG_PLANNED
		);
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
		ExecuteCommand(command);
	}
}

void CmdInit(const std::wstring& unlockPin) {
	g_unlockPin = unlockPin;
	OverlaySetPinCallback(OnPinEntered);
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
