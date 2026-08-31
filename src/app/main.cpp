#ifdef _WIN32
#include <windows.h>
#include <string>
#include "amt/core/Version.h"

namespace {
LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
  switch (message) {
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProc(hwnd, message, wparam, lparam);
  }
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show_command) {
  const wchar_t class_name[] = L"AudioMasteringToolWindow";
  WNDCLASSW wc{};
  wc.lpfnWndProc = window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = class_name;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  RegisterClassW(&wc);

  HWND hwnd = CreateWindowExW(
      0,
      class_name,
      L"AudioMasteringTool - Phase 0",
      WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT,
      CW_USEDEFAULT,
      1000,
      640,
      nullptr,
      nullptr,
      instance,
      nullptr);

  if (!hwnd) return 1;

  ShowWindow(hwnd, show_command);

  MSG msg{};
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  return static_cast<int>(msg.wParam);
}
#else
#include <iostream>
int main() {
  std::cout << "AudioMasteringTool desktop shell is Windows-first.\n";
  return 0;
}
#endif
