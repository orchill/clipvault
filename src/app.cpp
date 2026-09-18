#include "app.h"
#include "capture.h"
#include "imaging.h"
#include "popup.h"
#include "restore.h"
#include "settings_window.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>

#include <cstdio>

static FILE* alog() { static FILE* f = nullptr; if (!f) f = fopen("C:\\Users\\caner\\AppData\\Local\\ClipVault\\app.log", "a"); return f; }
#define ALOG(...) do { FILE* f = alog(); if (f) { fprintf(f, "%lu ", (unsigned long)GetTickCount64()); fprintf(f, __VA_ARGS__); fprintf(f, "\n"); fflush(f); } } while (0)



namespace cv {

static App g_app;
App& A() { return g_app; }

static UINT g_taskbarCreated = 0;  // RegisterWindowMessageW(L"TaskbarCreated")
static NOTIFYICONDATAW g_nid = {};
static bool g_trayAdded = false;

// ---------------------------------------------------------------- item ids
static i64 NewId() {
  App& a = A();
  return (i64)NowMs() * 1000 + (a.nextId++ % 1000);
}

// normalized text basis for dedup (CRLF/LF differences are not new content)
static u64 TextHash(const wstring& text) {
  if (text.find(L'\r') == wstring::npos) return Fnv64(text.data(), text.size() * 2);
  wstring norm;
  norm.reserve(text.size());
  for (size_t k = 0; k < text.size(); k++) {
    if (text[k] == L'\r') {
      if (k + 1 < text.size() && text[k + 1] == L'\n') continue;
      norm += L'\n';
    } else {
      norm += text[k];
    }
  }
  return Fnv64(norm.data(), norm.size() * 2);
}

// ---------------------------------------------------------------- init

bool App::Init(HINSTANCE hInst) {
  DataDir();  // create directories
  g_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
  if (hIconApp) DestroyIcon(hIconApp);
  hIconApp = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON,
                               GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);

  if (!CreatePopupWindow(hInst)) return false;

  ImagingStart(hwndMain, WM_APP_IMAGE_READY);
  storage.Load(history);
  history.AutoClean(Settings::I().autoCleanDays);

  ApplyMonitoring();
  ReapplyHotkey();
  UpdateTray();

  SetTimer(hwndMain, TIMER_CLEAN, 6ULL * 3600ULL * 1000ULL, nullptr);
  return true;
}

void App::Shutdown() {
  ImagingStop();
  PersistNow();
  if (g_trayAdded) {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
  }
  UnregisterHotKey(hwndMain, HOTKEY_ID);
  RemoveClipboardFormatListener(hwndMain);
}

void App::ExitApp() {
  Settings& s = Settings::I();
  if (s.clearOnExit) history.ClearAll();
  HidePopup(false);
  DestroyWindow(hwndMain);
}

// ---------------------------------------------------------------- tray

void App::UpdateTray() {
  Settings& s = Settings::I();
  if (s.showTray && !g_trayAdded) {
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwndMain;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_APP_TRAY;
    g_nid.hIcon = hIconApp;
    lstrcpynW(g_nid.szTip, L"ClipVault", ARRAYSIZE(g_nid.szTip));
    g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != 0;
    if (!g_trayAdded) {
      // try again with a newer struct size (older shell compatibility is not needed,
      // but NIM_ADD can transiently fail right after logon)
      Sleep(300);
      g_trayAdded = Shell_NotifyIconW(NIM_ADD, &g_nid) != 0;
    }
  } else if (!s.showTray && g_trayAdded) {
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    g_trayAdded = false;
  } else if (g_trayAdded) {
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
  }
}

void App::ShowFirstRunBalloon() {
  if (!g_trayAdded) return;
  NOTIFYICONDATAW nid = g_nid;
  nid.uFlags = NIF_INFO;
  lstrcpynW(nid.szInfoTitle, L"ClipVault is running", ARRAYSIZE(nid.szInfoTitle));
  Settings& s = Settings::I();
  wstring keys = HotkeyToString(s.hotkeyMods, s.hotkeyVk);
  wstring body;
  if (hotkeyOk)  // never claim a shortcut that is not actually registered
    body = L"Copying is being tracked in the background. Press " + keys +
           L" to open your clipboard history.";
  else
    body = L"Copying is being tracked in the background. The shortcut " + keys +
           L" could not be registered (another app may use it). Open Settings "
           L"from the tray to pick a different one.";
  lstrcpynW(nid.szInfo, body.c_str(), ARRAYSIZE(nid.szInfo));
  nid.dwInfoFlags = NIIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &nid);
}

// ---------------------------------------------------------------- popup show/hide

void App::TogglePopup() {
  if (IsWindowVisible(hwndMain))
    HidePopup(true);
  else
    ShowPopup();
}

void App::ShowPopup() {
  // remember where the user was so Enter can paste back into it
  prevWindow = GetForegroundWindow();
  prevFocus = prevWindow;
  GUITHREADINFO gti{};
  gti.cbSize = sizeof(gti);
  if (prevWindow && GetGUIThreadInfo(GetWindowThreadProcessId(prevWindow, nullptr), &gti) &&
      gti.hwndFocus)
    prevFocus = gti.hwndFocus;

  // reset transient state for a fresh view (search + selection)
  PopupReset();
  QueueMissingThumbs();

  // center on the monitor the user is working on
  HMONITOR mon = MonitorFromWindow(prevWindow ? prevWindow : hwndMain, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  RECT rc = {0, 0, ui.width, ui.height};
  if (GetMonitorInfoW(mon, &mi)) {
    int w = ui.width, h = ui.height;
    int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
    int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 3;
    // keep inside work area
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y < mi.rcWork.top) y = mi.rcWork.top;
    if (x + w > mi.rcWork.right) x = mi.rcWork.right - w;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    rc = {x, y, x + w, y + h};
    SetWindowPos(hwndMain, HWND_TOPMOST, rc.left, rc.top, w, h,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
  } else {
    SetWindowPos(hwndMain, HWND_TOPMOST, rc.left, rc.top, ui.width, ui.height,
                 SWP_SHOWWINDOW | SWP_NOACTIVATE);
  }

  // robust focus acquisition (hotkey does not grant foreground rights)
  DWORD prevThread = prevWindow ? GetWindowThreadProcessId(prevWindow, nullptr) : 0;
  DWORD ourThread = GetCurrentThreadId();
  if (prevThread && prevThread != ourThread) AttachThreadInput(ourThread, prevThread, TRUE);
  SetForegroundWindow(hwndMain);
  if (prevThread && prevThread != ourThread) AttachThreadInput(ourThread, prevThread, FALSE);
  SetFocus(hwndEdit);
}

void App::HidePopup(bool restoreFocus) {
  if (!IsWindowVisible(hwndMain)) return;
  PopupHidePreview();
  KillTimer(hwndMain, TIMER_PASTE);
  ShowWindow(hwndMain, SW_HIDE);
  if (restoreFocus && prevWindow && IsWindow(prevWindow)) {
    // re-activate the window the user came from; the OS restores its inner focus
    DWORD ourThread = GetCurrentThreadId();
    DWORD prevThread = GetWindowThreadProcessId(prevWindow, nullptr);
    if (prevThread && prevThread != ourThread) AttachThreadInput(ourThread, prevThread, TRUE);
    SetForegroundWindow(prevWindow);
    if (prevThread && prevThread != ourThread) AttachThreadInput(ourThread, prevThread, FALSE);
  }
}

// ---------------------------------------------------------------- capture pipeline

void App::OnClipboardUpdate() {
  ALOG("update: selfWrite=%d monitoring=%d", (int)selfWrite, (int)monitoring);
  if (selfWrite || !monitoring) return;
  Settings& s = Settings::I();

  CaptureResult c = CaptureClipboard();
  ALOG("captured ok=%d type=%d", (int)c.ok, (int)c.type);
  if (!c.ok) return;
  if (!c.srcApp.empty() && s.IsExcluded(ToLowerW(c.srcApp))) return;  // privacy exclusion

  u64 ts = NowMs();
  u64 hash = 0;
  switch (c.type) {
    case ItemType::Image:
      hash = !c.pngBytes.empty() ? Fnv64(c.pngBytes.data(), c.pngBytes.size()) : 0;
      break;
    case ItemType::Text:
      hash = TextHash(c.text);
      break;
    case ItemType::Files: {
      wstring joined;
      for (auto& f : c.files) {
        joined += f;
        joined += L'\n';
      }
      hash = TextHash(joined);
      break;
    }
    default:  // Rtf / Html hashed from raw bytes below
      break;
  }
  if (hash && hash == lastSelfHash && ts - lastSelfTs < 500) return;  // echo guard

  if (c.type == ItemType::Image) {
    auto job = std::make_shared<ImageJob>();
    job->item.id = NewId();
    job->item.type = ItemType::Image;
    job->item.ts = ts;
    job->item.srcApp = c.srcApp;
    job->item.text = L"";
    job->pngBytes = std::move(c.pngBytes);
    job->dibHeader = std::move(c.dibHeader);
    job->dibBits = std::move(c.dibBits);
    job->dibHasAlpha = c.dibHasAlpha;
    ImagingPost(std::move(job));
    return;  // history insertion happens in OnImageReady
  }

  Item it;
  it.id = NewId();
  it.type = c.type;
  it.ts = ts;
  it.srcApp = c.srcApp;
  it.size = c.contentSize;
  it.hash = hash;
  it.text = c.text.substr(0, Item::kTextRamCap);
  it.textLower = ToLowerW(it.text);

  // large payloads go to content-addressed blob files; metadata stays small
  auto writeBlob = [&](const string& ext, const void* data, size_t len) -> string {
    string name = HashToHex(it.hash) + ext;
    if (!WriteAllBytesAtomic(BlobDir() + L"\\" + Utf8ToUtf16(name), data, len)) return "";
    return name;
  };

  switch (c.type) {
    case ItemType::Text:
      if (c.text.size() > Item::kTextRamCap) {
        string u8 = Utf16ToUtf8(c.text);
        it.blobFile = writeBlob(".txt", u8.data(), u8.size());
      }
      break;
    case ItemType::Rtf:
      it.hash = Fnv64(c.rtfBytes.data(), c.rtfBytes.size());
      it.blobFile = writeBlob(".rtf", c.rtfBytes.data(), c.rtfBytes.size());
      break;
    case ItemType::Html:
      it.hash = Fnv64(c.htmlBytes.data(), c.htmlBytes.size());
      it.blobFile = writeBlob(".html", c.htmlBytes.data(), c.htmlBytes.size());
      break;
    case ItemType::Files:
      break;
    default:
      return;
  }

  int status = history.Add(std::move(it));
  ALOG("Add status=%d", status);
  if (status < 0) return;
  history.EnforceImageBudget((u64)s.maxImageMB << 20);
  SchedulePersist();
  PopupRefresh();
}

void App::QueueMissingThumbs() {
  // thumbnails are runtime-only; after a restart, rebuild them lazily off-thread
  for (auto& it : history.Items()) {
    if (it.type == ItemType::Image && !it.thumb && !it.thumbPending && !it.imgFile.empty()) {
      it.thumbPending = true;
      auto job = std::make_shared<ImageJob>();
      job->thumbOnly = true;
      job->item.id = it.id;
      job->item.imgFile = it.imgFile;
      job->item.imgJpeg = it.imgJpeg;
      ImagingPost(std::move(job));
    }
  }
}

void App::OnImageReady(void* result) {
  std::unique_ptr<ImageResult> res((ImageResult*)result);
  if (!res) return;
  if (res->thumbOnly) {
    Item* ex = history.Find(res->item.id);
    if (ex) {
      // leave thumbPending set when the blob failed to decode: the file is
      // gone/corrupt, so re-probing on every popup open would be wasted work
      ex->thumbPending = res->item.thumb == nullptr;
      if (!ex->thumb && res->item.thumb) ex->thumb = res->item.thumb;
      PopupRefresh();
    } else if (res->item.thumb) {
      delete (Gdiplus::Bitmap*)res->item.thumb;
    }
    return;
  }
  Settings& s = Settings::I();
  Item it = std::move(res->item);
  ALOG("OnImageReady: id=%lld img=%s", (long long)it.id, it.imgFile.c_str());
  int status = history.Add(std::move(it));
  if (status >= 0) {
    history.EnforceImageBudget((u64)s.maxImageMB << 20);
    SchedulePersist();
    PopupRefresh();
  }
}

// ---------------------------------------------------------------- actions

void App::RestoreAndPaste(Item* it) {
  ALOG("RestoreAndPaste: id=%lld type=%d img=%s", (long long)it->id, (int)it->type, it->imgFile.c_str());
  if (!it) return;
  selfWrite = true;
  bool ok = RestoreItemToClipboard(*it);
  selfWrite = false;
  if (ok) {
    lastSelfHash = it->hash;
    lastSelfTs = NowMs();
  }
  HidePopup(true);
  if (ok && Settings::I().autoPaste && prevWindow && IsWindow(prevWindow)) {
    SetForegroundWindow(prevWindow);
    SetTimer(hwndMain, TIMER_PASTE, 90, nullptr);
  }
}

void App::CopyAgain(const Item& it) {
  selfWrite = true;
  bool ok = RestoreItemToClipboard(it);
  selfWrite = false;
  if (ok) {
    lastSelfHash = it.hash;
    lastSelfTs = NowMs();
  }
}

void App::OnKey(KeyCmd cmd) {
  switch (cmd) {
    case KeyCmd::Esc:
      HidePopup(true);
      break;
    case KeyCmd::Enter:
      PopupActivateSelection();
      break;
    case KeyCmd::Delete:
      PopupDeleteSelection();
      break;
    case KeyCmd::Pin:
      PopupPinSelection();
      break;
    case KeyCmd::Copy:
      PopupCopySelection();
      break;
    case KeyCmd::Up:
    case KeyCmd::Down:
    case KeyCmd::PgUp:
    case KeyCmd::PgDn:
      PopupMoveSelection(cmd);
      break;
  }
}

// ---------------------------------------------------------------- persistence

void App::SchedulePersist() {
  SetTimer(hwndMain, TIMER_PERSIST, 1500, nullptr);  // coalesce bursts into one write
}

void App::PersistNow() {
  KillTimer(hwndMain, TIMER_PERSIST);
  bool ok = storage.Save(history);
  ALOG("save ok=%d", (int)ok);
  if (ok) {
    persistRetries = 0;
    return;
  }
  // write failed (disk full / locked file): retry a bounded number of times,
  // then stay silent until the next history change re-schedules persistence
  if (persistRetries < 3) {
    persistRetries++;
    SchedulePersist();
  }
}

void App::ApplyMonitoring() {
  Settings& s = Settings::I();
  monitoring = !s.pauseMonitoring;
  if (monitoring)
    AddClipboardFormatListener(hwndMain);
  else
    RemoveClipboardFormatListener(hwndMain);
}

bool App::ReapplyHotkey() {
  UnregisterHotKey(hwndMain, HOTKEY_ID);
  Settings& s = Settings::I();
  hotkeyOk = RegisterHotKey(hwndMain, HOTKEY_ID, s.hotkeyMods | MOD_NOREPEAT, s.hotkeyVk) != 0;
  return hotkeyOk;
}

void App::ApplySettingsChanged() {
  Settings& s = Settings::I();
  ApplyMonitoring();
  ReapplyHotkey();
  UpdateTray();
  ui.ApplyDpi(hwndMain, s.themeMode);
  SendMessageW(hwndEdit, WM_SETFONT, (WPARAM)ui.hUi, TRUE);
  history.EnforceImageBudget((u64)s.maxImageMB << 20);
  PopupRebuild();  // recompute metrics + view
  SetTimer(hwndMain, TIMER_CLEAN, 6ULL * 3600ULL * 1000ULL, nullptr);
}

// ---------------------------------------------------------------- wndproc plumbing

static void DoPersistTick(HWND hwnd) { A().PersistNow(); }

static void DoPasteTick() {
  // Ctrl+V into the restored focus target — but only if the user has not
  // changed focus during the delay; pasting into an unrelated window is worse
  // than not pasting. No polling: this runs once from the 90 ms timer.
  App& a = A();
  if (!a.prevWindow || !IsWindow(a.prevWindow)) return;
  if (GetForegroundWindow() != a.prevWindow) return;
  INPUT in[4]{};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = VK_CONTROL;
  in[1].type = INPUT_KEYBOARD;
  in[1].ki.wVk = 'V';
  in[2].type = INPUT_KEYBOARD;
  in[2].ki.wVk = 'V';
  in[2].ki.dwFlags = KEYEVENTF_KEYUP;
  in[3].type = INPUT_KEYBOARD;
  in[3].ki.wVk = VK_CONTROL;
  in[3].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(4, in, sizeof(INPUT));
}

static void ShowTrayMenu(HWND hwnd) {
  HMENU m = CreatePopupMenu();
  Settings& s = Settings::I();
  AppendMenuW(m, MF_STRING, 1001, L"Open clipboard history");
  AppendMenuW(m, MF_STRING, 1002, L"Settings");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  UINT pauseFlags = MF_STRING | (s.pauseMonitoring ? MF_CHECKED : 0);
  AppendMenuW(m, pauseFlags, 1003, L"Pause clipboard monitoring");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, 1004, L"Clear unpinned history");
  AppendMenuW(m, MF_STRING, 1005, L"Clear all history");
  AppendMenuW(m, MF_STRING, 1006, L"Open data folder");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  UINT runFlags = MF_STRING | (s.startWithWindows ? MF_CHECKED : 0);
  AppendMenuW(m, runFlags, 1007, L"Start with Windows");
  AppendMenuW(m, MF_STRING, 1008, L"Exit");

  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(hwnd);  // menus dismiss properly when the window is foreground
  int cmd = TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY, pt.x, pt.y, 0,
                           hwnd, nullptr);
  PostMessageW(hwnd, WM_NULL, 0, 0);
  DestroyMenu(m);

  App& a = A();
  switch (cmd) {
    case 1001: a.ShowPopup(); break;
    case 1002: OpenSettingsWindow(); break;
    case 1003:
      s.pauseMonitoring = !s.pauseMonitoring;
      s.Save();
      a.ApplyMonitoring();
      PopupRebuild();
      break;
    case 1004:
      a.history.ClearUnpinned();
      a.SchedulePersist();
      PopupRefresh();
      break;
    case 1005:
      a.history.ClearAll();
      a.SchedulePersist();
      PopupRefresh();
      break;
    case 1006:
      ShellExecuteW(nullptr, L"open", DataDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case 1007:
      s.startWithWindows = !s.startWithWindows;
      s.ApplyRunKey();
      s.Save();
      break;
    case 1008: a.ExitApp(); break;
    default: break;
  }
}

LRESULT AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  App& a = A();
  switch (msg) {
    case WM_CLIPBOARDUPDATE:
      a.OnClipboardUpdate();
      return 0;

    case WM_HOTKEY:
      if (wParam == HOTKEY_ID) a.TogglePopup();
      return 0;

    case WM_APP_IMAGE_READY:
      a.OnImageReady((void*)lParam);
      return 0;

    case WM_APP_KEY:
      a.OnKey((KeyCmd)wParam);
      return 0;

    case WM_APP_SHOWPOPUP:
      a.TogglePopup();
      return 0;

    case WM_APP_TRAY:
      if (lParam == WM_LBUTTONUP || lParam == WM_LBUTTONDBLCLK) a.TogglePopup();
      else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) ShowTrayMenu(hwnd);
      return 0;

    case WM_APP_SETTINGS_SAVED:
      a.ApplySettingsChanged();
      return 0;

    case WM_APP_OPENSETTINGS:
      if ((LPARAM)lParam == OPENSETTINGS_MAGIC) OpenSettingsWindow();
      return 0;

    case WM_TIMER:
      if (wParam == TIMER_PERSIST) DoPersistTick(hwnd);
      else if (wParam == TIMER_PASTE) {
        KillTimer(hwnd, TIMER_PASTE);
        DoPasteTick();
      } else if (wParam == TIMER_CLEAN) {
        a.history.AutoClean(Settings::I().autoCleanDays);
        a.SchedulePersist();
        PopupRefresh();
      }
      return 0;

    case WM_QUERYENDSESSION:
      a.PersistNow();
      return TRUE;

    case WM_ENDSESSION:
      if (wParam) a.PersistNow();
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      if (g_taskbarCreated && msg == g_taskbarCreated) {
        a.UpdateTray();  // explorer restarted: re-add the tray icon
        return 0;
      }
      return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
}

}  // namespace cv
