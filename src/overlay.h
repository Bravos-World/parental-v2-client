#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>

void OverlayCreate(HINSTANCE hInst);
void OverlayDestroy();
void OverlayShow(const std::wstring& statusText);
void OverlayHide();
bool OverlayIsVisible();
void OverlaySetStatus(const std::wstring& text);
void OverlayStartCountdown(int seconds, const std::wstring& commandLabel);
void OverlayStopCountdown();
void OverlaySetPinCallback(void(*onPinEntered)(const std::wstring& pin));
void OverlaySetActionCallback(void(*onAction)(const std::wstring& action));
HWND OverlayGetHwnd();
void OverlayShowMessage(const std::wstring& title, const std::wstring& message);
void OverlayHideMessage();
