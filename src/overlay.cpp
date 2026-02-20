#include "overlay.h"
#include "logger.h"
#include <thread>
#include <atomic>
#include <mutex>

#define IDC_PIN_EDIT 1001
#define IDC_PIN_BUTTON 1002
#define IDC_STATUS_STATIC 1003
#define IDC_COUNTDOWN_STATIC 1004
#define IDC_HINT_STATIC 1005
#define IDC_RESTART_BUTTON 1006
#define IDC_SHUTDOWN_BUTTON 1007
#define IDC_MSG_TITLE   1008
#define IDC_MSG_CONTENT 1009
#define IDC_MSG_DISMISS 1010
#define COUNTDOWN_TIMER_ID 5001
#define TOPMOST_TIMER_ID 5002

static HWND g_overlayWnd = nullptr;
static HWND g_statusLabel = nullptr;
static HWND g_countdownLabel = nullptr;
static HWND g_hintLabel = nullptr;
static HWND g_pinEdit = nullptr;
static HWND g_pinButton = nullptr;
static HWND g_restartButton = nullptr;
static HWND g_shutdownButton = nullptr;
static HWND g_msgWnd = nullptr;
static HWND g_msgTitleLabel = nullptr;
static HWND g_msgContentLabel = nullptr;
static HWND g_msgDismissBtn = nullptr;
static HFONT g_fontLarge = nullptr;
static HFONT g_fontMedium = nullptr;
static HFONT g_fontSmall = nullptr;
static HBRUSH g_bgBrush = nullptr;
static HBRUSH g_btnBrushRed = nullptr;
static HBRUSH g_btnBrushOrange = nullptr;
static HBRUSH g_msgBrush = nullptr;
static HHOOK g_kbHook = nullptr;
static HHOOK g_msHook = nullptr;
static std::atomic<bool> g_visible{false};
static std::atomic<int> g_countdownRemaining{0};
static std::wstring g_countdownCommand;
static std::mutex g_overlayMutex;

static void(*g_pinCallback)(const std::wstring& pin) = nullptr;
static void(*g_actionCallback)(const std::wstring& action) = nullptr;

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_visible.load()) {
        KBDLLHOOKSTRUCT* kb = (KBDLLHOOKSTRUCT*)lParam;
        bool isAlt = (kb->flags & LLKHF_ALTDOWN) != 0;

        if (kb->vkCode == VK_ESCAPE ||
            (kb->vkCode == VK_TAB && isAlt) ||
            (kb->vkCode == VK_F4 && isAlt) ||
            kb->vkCode == VK_LWIN || kb->vkCode == VK_RWIN ||
            (kb->vkCode == VK_DELETE && (GetAsyncKeyState(VK_CONTROL) & 0x8000) && isAlt)) {
            return 1;
        }

        HWND hwndFocus = GetFocus();
        if (hwndFocus == g_pinEdit) {
            return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
        }

        if (kb->vkCode == VK_TAB && wParam == WM_KEYDOWN) {
            SetFocus(g_pinEdit);
            return 1;
        }
        if (kb->vkCode == VK_RETURN && wParam == WM_KEYDOWN) {
            PostMessageW(g_overlayWnd, WM_COMMAND, MAKEWPARAM(IDC_PIN_BUTTON, BN_CLICKED), (LPARAM)g_pinButton);
            return 1;
        }

        return 1;
    }
    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_visible.load() && g_overlayWnd) {
        if (wParam == WM_MOUSEMOVE) {
            return CallNextHookEx(g_msHook, nCode, wParam, lParam);
        }

        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lParam;
        POINT pt = ms->pt;
        RECT pinEditRect = {}, pinBtnRect = {}, restartRect = {}, shutdownRect = {}, msgRect = {};
        if (g_pinEdit) GetWindowRect(g_pinEdit, &pinEditRect);
        if (g_pinButton) GetWindowRect(g_pinButton, &pinBtnRect);
        if (g_restartButton) GetWindowRect(g_restartButton, &restartRect);
        if (g_shutdownButton) GetWindowRect(g_shutdownButton, &shutdownRect);
        bool msgVisible = g_msgWnd && IsWindowVisible(g_msgWnd);
        if (msgVisible) GetWindowRect(g_msgWnd, &msgRect);

        if (PtInRect(&pinEditRect, pt) || PtInRect(&pinBtnRect, pt) ||
            PtInRect(&restartRect, pt) || PtInRect(&shutdownRect, pt) ||
            (msgVisible && PtInRect(&msgRect, pt))) {
            if (wParam == WM_LBUTTONDOWN && PtInRect(&pinEditRect, pt)) {
                SetFocus(g_pinEdit);
            }
            return CallNextHookEx(g_msHook, nCode, wParam, lParam);
        }
        return 1;
    }
    return CallNextHookEx(g_msHook, nCode, wParam, lParam);
}

static LRESULT CALLBACK MsgPanelWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (!g_msgBrush) g_msgBrush = CreateSolidBrush(RGB(20, 20, 50));
        FillRect(hdc, &rc, g_msgBrush);
        return 1;
    }
    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        if (!g_msgBrush) g_msgBrush = CreateSolidBrush(RGB(20, 20, 50));
        return (LRESULT)g_msgBrush;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_MSG_DISMISS)
            ShowWindow(hWnd, SW_HIDE);
        return 0;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

static void InstallHooks() {
    if (!g_kbHook) {
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    }
    if (!g_msHook) {
        g_msHook = SetWindowsHookExW(WH_MOUSE_LL, LowLevelMouseProc, GetModuleHandleW(nullptr), 0);
    }
}

static void RemoveHooks() {
    if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = nullptr; }
    if (g_msHook) { UnhookWindowsHookEx(g_msHook); g_msHook = nullptr; }
}

static LRESULT CALLBACK OverlayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE:
        return 0;

    case WM_CLOSE:
        return 0;

    case WM_DESTROY:
        return 0;

    case WM_CTLCOLORSTATIC: {
        HDC hdc = (HDC)wParam;
        SetTextColor(hdc, RGB(255, 255, 255));
        SetBkMode(hdc, TRANSPARENT);
        if (!g_bgBrush) g_bgBrush = CreateSolidBrush(RGB(30, 30, 60));
        return (LRESULT)g_bgBrush;
    }

    case WM_CTLCOLORBTN: {
        HWND hCtrl = (HWND)lParam;
        HDC hdc = (HDC)wParam;
        if (hCtrl == g_restartButton) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            if (!g_btnBrushOrange) g_btnBrushOrange = CreateSolidBrush(RGB(200, 100, 0));
            return (LRESULT)g_btnBrushOrange;
        }
        if (hCtrl == g_shutdownButton) {
            SetTextColor(hdc, RGB(255, 255, 255));
            SetBkMode(hdc, TRANSPARENT);
            if (!g_btnBrushRed) g_btnBrushRed = CreateSolidBrush(RGB(180, 30, 30));
            return (LRESULT)g_btnBrushRed;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    case WM_ERASEBKGND: {
        HDC hdc = (HDC)wParam;
        RECT rc;
        GetClientRect(hWnd, &rc);
        if (!g_bgBrush) g_bgBrush = CreateSolidBrush(RGB(30, 30, 60));
        FillRect(hdc, &rc, g_bgBrush);
        return 1;
    }

    case WM_TIMER:
        if (wParam == COUNTDOWN_TIMER_ID) {
            int remaining = g_countdownRemaining.load();
            if (remaining > 0) {
                remaining--;
                g_countdownRemaining.store(remaining);
                WCHAR buf[256];
                wsprintfW(buf, L"%s in %d seconds...", g_countdownCommand.c_str(), remaining);
                SetWindowTextW(g_countdownLabel, buf);
            }
            if (remaining <= 0) {
                KillTimer(hWnd, COUNTDOWN_TIMER_ID);
            }
        }
        if (wParam == TOPMOST_TIMER_ID && g_visible.load()) {
            SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (GetForegroundWindow() != hWnd) {
                SetForegroundWindow(hWnd);
                SetFocus(g_pinEdit);
            }
        }
        return 0;

    case WM_DRAWITEM: {
        DRAWITEMSTRUCT* dis = (DRAWITEMSTRUCT*)lParam;
        if (dis->CtlID == IDC_RESTART_BUTTON || dis->CtlID == IDC_SHUTDOWN_BUTTON) {
            COLORREF clr = (dis->CtlID == IDC_RESTART_BUTTON) ? RGB(200, 100, 0) : RGB(180, 30, 30);
            if (dis->itemState & ODS_SELECTED) {
                clr = (dis->CtlID == IDC_RESTART_BUTTON) ? RGB(160, 80, 0) : RGB(140, 20, 20);
            }
            HBRUSH hbr = CreateSolidBrush(clr);
            FillRect(dis->hDC, &dis->rcItem, hbr);
            DeleteObject(hbr);
            WCHAR text[32] = {};
            GetWindowTextW(dis->hwndItem, text, 32);
            SetTextColor(dis->hDC, RGB(255, 255, 255));
            SetBkMode(dis->hDC, TRANSPARENT);
            SelectObject(dis->hDC, g_fontSmall);
            DrawTextW(dis->hDC, text, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (dis->itemState & ODS_FOCUS) {
                DrawFocusRect(dis->hDC, &dis->rcItem);
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
        if (HIWORD(wParam) == BN_CLICKED) {
            if (LOWORD(wParam) == IDC_RESTART_BUTTON) {
                LogInfo("Overlay: Restart button clicked");
                if (g_actionCallback) g_actionCallback(L"RESTART");
            } else if (LOWORD(wParam) == IDC_SHUTDOWN_BUTTON) {
                LogInfo("Overlay: Shutdown button clicked");
                if (g_actionCallback) g_actionCallback(L"SHUTDOWN");
            } else if (LOWORD(wParam) == IDC_PIN_BUTTON) {
                WCHAR pinBuf[64] = {};
                GetWindowTextW(g_pinEdit, pinBuf, 64);
                SetWindowTextW(g_pinEdit, L"");
                if (g_pinCallback) g_pinCallback(std::wstring(pinBuf));
            }
        }
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

void OverlayCreate(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = OverlayWndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
    wc.lpszClassName = L"ParentClientOverlay";
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    g_fontLarge = CreateFontW(48, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontMedium = CreateFontW(28, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    g_fontSmall = CreateFontW(20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);

    WNDCLASSEXW wcMsg = {};
    wcMsg.cbSize = sizeof(wcMsg);
    wcMsg.style = CS_HREDRAW | CS_VREDRAW;
    wcMsg.lpfnWndProc = MsgPanelWndProc;
    wcMsg.hInstance = hInst;
    wcMsg.lpszClassName = L"ParentClientMsgPanel";
    RegisterClassExW(&wcMsg);

    g_overlayWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TOOLWINDOW,
        L"ParentClientOverlay", L"",
        WS_POPUP,
        0, 0, sw, sh,
        nullptr, nullptr, hInst, nullptr);

    SetLayeredWindowAttributes(g_overlayWnd, 0, 230, LWA_ALPHA);

    int cx = sw / 2;
    int cy = sh / 2;

    g_msgWnd = CreateWindowExW(
        0, L"ParentClientMsgPanel", L"",
        WS_CHILD,
        cx - 200, 30, 400, 130,
        g_overlayWnd, nullptr, hInst, nullptr);

    g_msgTitleLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 10, 380, 30,
        g_msgWnd, (HMENU)IDC_MSG_TITLE, hInst, nullptr);
    SendMessageW(g_msgTitleLabel, WM_SETFONT, (WPARAM)g_fontMedium, TRUE);

    g_msgContentLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        10, 46, 380, 40,
        g_msgWnd, (HMENU)IDC_MSG_CONTENT, hInst, nullptr);
    SendMessageW(g_msgContentLabel, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_msgDismissBtn = CreateWindowExW(0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        150, 92, 100, 28,
        g_msgWnd, (HMENU)IDC_MSG_DISMISS, hInst, nullptr);
    SendMessageW(g_msgDismissBtn, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_statusLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx - 400, cy - 120, 800, 60,
        g_overlayWnd, (HMENU)IDC_STATUS_STATIC, hInst, nullptr);
    SendMessageW(g_statusLabel, WM_SETFONT, (WPARAM)g_fontLarge, TRUE);

    g_countdownLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx - 400, cy - 50, 800, 40,
        g_overlayWnd, (HMENU)IDC_COUNTDOWN_STATIC, hInst, nullptr);
    SendMessageW(g_countdownLabel, WM_SETFONT, (WPARAM)g_fontMedium, TRUE);

    g_hintLabel = CreateWindowExW(0, L"STATIC", L"Enter PIN to unlock:",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        cx - 200, cy - 8, 400, 24,
        g_overlayWnd, (HMENU)IDC_HINT_STATIC, hInst, nullptr);
    SendMessageW(g_hintLabel, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_pinEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_CENTER | ES_PASSWORD | ES_AUTOHSCROLL,
        cx - 120, cy + 22, 180, 32,
        g_overlayWnd, (HMENU)IDC_PIN_EDIT, hInst, nullptr);
    SendMessageW(g_pinEdit, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_pinButton = CreateWindowExW(0, L"BUTTON", L"Unlock",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        cx + 70, cy + 22, 80, 32,
        g_overlayWnd, (HMENU)IDC_PIN_BUTTON, hInst, nullptr);
    SendMessageW(g_pinButton, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_restartButton = CreateWindowExW(0, L"BUTTON", L"Restart",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        cx - 115, cy + 74, 100, 32,
        g_overlayWnd, (HMENU)IDC_RESTART_BUTTON, hInst, nullptr);
    SendMessageW(g_restartButton, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    g_shutdownButton = CreateWindowExW(0, L"BUTTON", L"Shutdown",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | BS_OWNERDRAW,
        cx + 15, cy + 74, 100, 32,
        g_overlayWnd, (HMENU)IDC_SHUTDOWN_BUTTON, hInst, nullptr);
    SendMessageW(g_shutdownButton, WM_SETFONT, (WPARAM)g_fontSmall, TRUE);

    ShowWindow(g_overlayWnd, SW_HIDE);
}

void OverlayDestroy() {
    OverlayHide();
    if (g_overlayWnd) { DestroyWindow(g_overlayWnd); g_overlayWnd = nullptr; }
    if (g_fontLarge) { DeleteObject(g_fontLarge); g_fontLarge = nullptr; }
    if (g_fontMedium) { DeleteObject(g_fontMedium); g_fontMedium = nullptr; }
    if (g_fontSmall) { DeleteObject(g_fontSmall); g_fontSmall = nullptr; }
    if (g_bgBrush) { DeleteObject(g_bgBrush); g_bgBrush = nullptr; }
    if (g_btnBrushRed) { DeleteObject(g_btnBrushRed); g_btnBrushRed = nullptr; }
    if (g_btnBrushOrange) { DeleteObject(g_btnBrushOrange); g_btnBrushOrange = nullptr; }
    if (g_msgBrush) { DeleteObject(g_msgBrush); g_msgBrush = nullptr; }
}

void OverlayShow(const std::wstring& statusText) {
    if (!g_overlayWnd) return;
    LogInfoW(L"Showing overlay: " + statusText);
    SetWindowTextW(g_statusLabel, statusText.c_str());
    SetWindowTextW(g_countdownLabel, L"");
    SetWindowTextW(g_hintLabel, L"Enter PIN to unlock:");
    SetWindowTextW(g_pinEdit, L"");

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    SetWindowPos(g_overlayWnd, HWND_TOPMOST, 0, 0, sw, sh, SWP_SHOWWINDOW);
    ShowWindow(g_overlayWnd, SW_SHOW);
    SetForegroundWindow(g_overlayWnd);
    SetFocus(g_pinEdit);
    g_visible.store(true);
    InstallHooks();
    SetTimer(g_overlayWnd, TOPMOST_TIMER_ID, 500, nullptr);
}

void OverlayHide() {
    if (!g_overlayWnd) return;
    LogInfo("Hiding overlay");
    RemoveHooks();
    g_visible.store(false);
    OverlayStopCountdown();
    KillTimer(g_overlayWnd, TOPMOST_TIMER_ID);
    OverlayHideMessage();
    ShowWindow(g_overlayWnd, SW_HIDE);
}

bool OverlayIsVisible() {
    return g_visible.load();
}

void OverlaySetStatus(const std::wstring& text) {
    if (g_statusLabel) SetWindowTextW(g_statusLabel, text.c_str());
}

void OverlayStartCountdown(int seconds, const std::wstring& commandLabel) {
    g_countdownRemaining.store(seconds);
    g_countdownCommand = commandLabel;
    WCHAR buf[256];
    wsprintfW(buf, L"%s in %d seconds...", commandLabel.c_str(), seconds);
    SetWindowTextW(g_countdownLabel, buf);
    SetTimer(g_overlayWnd, COUNTDOWN_TIMER_ID, 1000, nullptr);
}

void OverlayStopCountdown() {
    if (g_overlayWnd) KillTimer(g_overlayWnd, COUNTDOWN_TIMER_ID);
    g_countdownRemaining.store(0);
    if (g_countdownLabel) SetWindowTextW(g_countdownLabel, L"");
}

void OverlaySetPinCallback(void(*onPinEntered)(const std::wstring& pin)) {
    g_pinCallback = onPinEntered;
}

void OverlaySetActionCallback(void(*onAction)(const std::wstring& action)) {
    g_actionCallback = onAction;
}

HWND OverlayGetHwnd() {
    return g_overlayWnd;
}

void OverlayShowMessage(const std::wstring& title, const std::wstring& message) {
    if (!g_msgWnd) return;
    SetWindowTextW(g_msgTitleLabel, title.c_str());
    SetWindowTextW(g_msgContentLabel, message.c_str());
    ShowWindow(g_msgWnd, SW_SHOW);
    SetWindowPos(g_msgWnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
    InvalidateRect(g_msgWnd, nullptr, TRUE);
    UpdateWindow(g_msgWnd);
}

void OverlayHideMessage() {
    if (g_msgWnd) ShowWindow(g_msgWnd, SW_HIDE);
}
