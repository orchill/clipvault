// main.cpp — entry point: single instance, GDI+ startup, message loop.
#include "app.h"
#include "settings.h"

#include <commctrl.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

using namespace cv;

static bool AnotherInstanceRunning() {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\ClipVault.SingleInstance");
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return true;
  }
  // ownership held for process lifetime (leaked on exit by design — OS cleans up)
  return false;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
  if (AnotherInstanceRunning()) {
    // bring up the history popup of the running instance
    HWND other = FindWindowW(L"ClipVaultPopup", nullptr);
    if (other) PostMessageW(other, WM_APP_SHOWPOPUP, 0, 0);
    return 0;
  }

  ULONG_PTR gdiToken = 0;
  Gdiplus::GdiplusStartupInput gsi;
  if (GdiplusStartup(&gdiToken, &gsi, nullptr) != Gdiplus::Ok) return 1;

  INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
  InitCommonControlsEx(&icc);

  Settings::I().Load();

  if (!A().Init(hInst)) {
    Gdiplus::GdiplusShutdown(gdiToken);
    return 1;
  }

  if (Settings::I().firstRun) {
    A().ShowFirstRunBalloon();
    Settings::I().firstRun = false;
    Settings::I().Save();
  }

  bool background = nCmdShow == SW_HIDE || Settings::I().startInBackground;
  if (!background) A().ShowPopup();

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }

  A().Shutdown();
  Gdiplus::GdiplusShutdown(gdiToken);
  return 0;
}
