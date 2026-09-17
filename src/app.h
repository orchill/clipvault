// app.h — application orchestrator: wiring between capture/imaging/history/UI/tray.
#pragma once
#include "history.h"
#include "settings.h"
#include "storage.h"
#include "theme.h"

namespace cv {

// WM_APP message codes
constexpr UINT WM_APP_IMAGE_READY = WM_APP + 1;  // lParam = ImageResult* (heap; UI frees)
constexpr UINT WM_APP_TRAY = WM_APP + 2;         // tray icon callbacks
constexpr UINT WM_APP_KEY = WM_APP + 3;          // wParam = KeyCmd from search edit
constexpr UINT WM_APP_SHOWPOPUP = WM_APP + 4;    // second instance / tray
constexpr UINT WM_APP_SETTINGS_SAVED = WM_APP + 5;
constexpr UINT WM_APP_OPENSETTINGS = WM_APP + 6;
// automation-only: requires this magic in lParam so stray window messages
// in the WM_APP range can never open the settings window
constexpr LPARAM OPENSETTINGS_MAGIC = 0x434C5631;

constexpr int TIMER_PERSIST = 1;   // debounced metadata flush
constexpr int TIMER_PASTE = 2;     // deferred Ctrl+V after focus restore
constexpr int TIMER_CLEAN = 3;     // periodic auto-cleanup (6h)
constexpr int HOTKEY_ID = 1;

enum class KeyCmd { Up, Down, PgUp, PgDn, Enter, Esc, Delete, Pin, Copy };

struct App {
  History history;
  Storage storage;
  UiCtx ui;

  HWND hwndMain = nullptr;  // popup window (doubles as message/tray/hotkey window)
  HWND hwndEdit = nullptr;  // search edit (child)
  HICON hIconApp = nullptr;

  bool monitoring = true;  // false when paused
  bool selfWrite = false;  // guard: ignore clipboard updates we caused
  u64 lastSelfHash = 0;
  u64 lastSelfTs = 0;
  i64 nextId = 1;

  HWND prevWindow = nullptr;  // focus restore targets
  HWND prevFocus = nullptr;
  void* popup = nullptr;      // PopupState* (opaque, owned by popup.cpp)

  bool Init(HINSTANCE hInst);
  void Shutdown();
  void ExitApp();

  void TogglePopup();
  void ShowPopup();
  void HidePopup(bool restoreFocus);

  void OnClipboardUpdate();
  void OnImageReady(void* result);
  void OnKey(KeyCmd cmd);
  void RestoreAndPaste(Item* it);   // set clipboard + close + optional Ctrl+V
  void CopyAgain(const Item& it);   // set clipboard only

  void SchedulePersist();
  void PersistNow();
  void ApplyMonitoring();
  bool ReapplyHotkey();
  void ApplySettingsChanged();  // after settings window saves
  void UpdateTray();
  void ShowFirstRunBalloon();
  void QueueMissingThumbs();
};

App& A();

LRESULT AppWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

}  // namespace cv
