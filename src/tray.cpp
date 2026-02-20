#include "tray.h"
#include <shellapi.h>

#define WM_TRAYICON (WM_USER + 100)
#define IDM_STATUS 2001
#define IDM_EXIT   2002
#define TRAY_ICON_ID 1

static NOTIFYICONDATAW g_nid = {};
static HWND g_trayHwnd = nullptr;
static std::wstring g_statusText = L"Initializing...";
static std::wstring g_exitPin;
static HINSTANCE g_hInst = nullptr;

static INT_PTR CALLBACK PinDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"Enter PIN to Exit");
        HWND hLabel = CreateWindowExW(0, L"STATIC", L"PIN:",
            WS_CHILD | WS_VISIBLE, 10, 15, 40, 20, hDlg, nullptr, g_hInst, nullptr);
        HWND hEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_PASSWORD | ES_AUTOHSCROLL,
            55, 12, 150, 24, hDlg, (HMENU)3001, g_hInst, nullptr);
        HWND hOk = CreateWindowExW(0, L"BUTTON", L"OK",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
            60, 50, 70, 28, hDlg, (HMENU)IDOK, g_hInst, nullptr);
        HWND hCancel = CreateWindowExW(0, L"BUTTON", L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            140, 50, 70, 28, hDlg, (HMENU)IDCANCEL, g_hInst, nullptr);
        SetFocus(hEdit);
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            WCHAR buf[64] = {};
            GetDlgItemTextW(hDlg, 3001, buf, 64);
            if (std::wstring(buf) == g_exitPin) {
                EndDialog(hDlg, IDOK);
            } else {
                MessageBoxW(hDlg, L"Incorrect PIN", L"Error", MB_OK | MB_ICONERROR);
            }
            return TRUE;
        }
        if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

static void ShowPinDialog() {
    HGLOBAL hGlobal = GlobalAlloc(GMEM_ZEROINIT, 1024);
    if (!hGlobal) return;
    DLGTEMPLATE* dlgTemplate = (DLGTEMPLATE*)GlobalLock(hGlobal);
    dlgTemplate->style = DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU;
    dlgTemplate->cx = 160;
    dlgTemplate->cy = 60;
    dlgTemplate->cdit = 0;
    GlobalUnlock(hGlobal);

    INT_PTR result = DialogBoxIndirectW(g_hInst, dlgTemplate, g_trayHwnd, PinDlgProc);
    GlobalFree(hGlobal);

    if (result == IDOK) {
        PostQuitMessage(0);
    }
}

static LRESULT CALLBACK TrayWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuW(hMenu, MF_STRING, IDM_STATUS, g_statusText.c_str());
            EnableMenuItem(hMenu, IDM_STATUS, MF_GRAYED);
            AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(hMenu, MF_STRING, IDM_EXIT, L"Exit");
            SetForegroundWindow(hWnd);
            TrackPopupMenu(hMenu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN,
                pt.x, pt.y, 0, hWnd, nullptr);
            DestroyMenu(hMenu);
        }
        return 0;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDM_EXIT) {
            ShowPinDialog();
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 0;

    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
}

void TrayCreate(HINSTANCE hInst, HWND hWndParent) {
    g_hInst = hInst;

    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = TrayWndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ParentClientTrayWnd";
    RegisterClassExW(&wc);

    g_trayHwnd = CreateWindowExW(0, L"ParentClientTrayWnd", L"",
        WS_OVERLAPPED, 0, 0, 0, 0, hWndParent, nullptr, hInst, nullptr);

    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_trayHwnd;
    g_nid.uID = TRAY_ICON_ID;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32518));
    wcscpy_s(g_nid.szTip, L"Parental Control Client");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    g_nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &g_nid);
}

void TrayDestroy() {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    if (g_trayHwnd) { DestroyWindow(g_trayHwnd); g_trayHwnd = nullptr; }
}

void TrayShowBalloon(const std::wstring& title, const std::wstring& message) {
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP | NIF_INFO;
    wcscpy_s(g_nid.szInfoTitle, title.c_str());
    wcscpy_s(g_nid.szInfo, message.c_str());
    g_nid.dwInfoFlags = NIIF_INFO;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

void TraySetStatusText(const std::wstring& text) {
    g_statusText = text;
}

void TraySetPinForExit(const std::wstring& pin) {
    g_exitPin = pin;
}
