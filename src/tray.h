#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

void TrayCreate(HINSTANCE hInst, HWND hWndParent);
void TrayDestroy();
void TrayShowBalloon(const std::wstring& title, const std::wstring& message);
void TraySetStatusText(const std::wstring& text);
void TraySetPinForExit(const std::wstring& pin);
