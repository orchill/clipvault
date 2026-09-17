// settings.h — user configuration (config.json) + Run-key integration.
#pragma once
#include "util.h"

namespace cv {

enum class ThemeMode { System = 0, Dark = 1, Light = 2 };
enum class DupMode { MoveTop = 0, KeepPos = 1, Allow = 2 };

struct Settings {
  // General
  UINT hotkeyMods = MOD_CONTROL | MOD_SHIFT;  // MOD_* flags
  UINT hotkeyVk = 'V';
  bool autoPaste = true;        // send Ctrl+V to the previous window after select
  bool showTray = true;
  bool startWithWindows = false;
  bool startInBackground = true;  // false -> popup is shown once at launch

  // History
  int maxItems = 25;            // max unpinned entries (spec default 25)
  bool persist = true;          // "Save clipboard history between restarts"
  int dupMode = (int)DupMode::MoveTop;
  int autoCleanDays = 0;        // 0 = never; otherwise clear unpinned items older than N days
  int maxImageMB = 200;         // total blob budget for images

  // Privacy
  bool pauseMonitoring = false;
  bool clearOnExit = false;
  std::vector<wstring> excludedApps;  // lower-case exe names, e.g. "keepass.exe"

  // Appearance
  int themeMode = (int)ThemeMode::System;
  bool compactRows = false;
  bool hoverPreview = true;  // large image preview on hover (v2)

  bool firstRun = true;

  bool Load();
  bool Save() const;
  void ApplyRunKey();  // writes/removes HKCU Run entry per startWithWindows
  bool IsExcluded(const wstring& exeNameLower) const;
  static Settings& I() {
    static Settings s;
    return s;
  }
};

wstring HotkeyToString(UINT mods, UINT vk);

}  // namespace cv
