#include "settings.h"


namespace cv {

static wstring ConfigPath() { return DataDir() + L"\\config.json"; }

bool Settings::IsExcluded(const wstring& exeNameLower) const {
  for (auto& e : excludedApps)
    if (!e.empty() && exeNameLower.find(e) != wstring::npos) return true;
  return false;
}

bool Settings::Load() {
  std::vector<u8> raw;
  if (!ReadAllBytes(ConfigPath(), raw) || raw.empty()) return false;
  // tolerate a UTF-8 BOM (e.g. files edited in Notepad)
  const char* jdata = (const char*)raw.data();
  size_t jlen = raw.size();
  if (jlen >= 3 && (u8)jdata[0] == 0xEF && (u8)jdata[1] == 0xBB && (u8)jdata[2] == 0xBF) {
    jdata += 3;
    jlen -= 3;
  }
  Json j;
  if (!Json::parse(jdata, jlen, j)) return false;  // corrupt -> defaults

  const Json* hk = j.get(L"hotkey");
  if (hk && hk->t == Json::OBJ) {
    hotkeyMods = (UINT)hk->asInt(L"mod", (i64)hotkeyMods);
    hotkeyVk = (UINT)hk->asInt(L"vk", (i64)hotkeyVk);
  }
  autoPaste = j.get(L"autoPaste") ? j.get(L"autoPaste")->asBool(autoPaste) : autoPaste;
  showTray = j.get(L"showTray") ? j.get(L"showTray")->asBool(showTray) : showTray;
  startWithWindows =
      j.get(L"startWithWindows") ? j.get(L"startWithWindows")->asBool(startWithWindows) : startWithWindows;
  startInBackground =
      j.get(L"startInBackground") ? j.get(L"startInBackground")->asBool(startInBackground) : startInBackground;
  maxItems = (int)j.asInt(L"maxItems", maxItems);
  if (maxItems < 5) maxItems = 5;
  if (maxItems > 500) maxItems = 500;
  persist = j.get(L"persist") ? j.get(L"persist")->asBool(persist) : persist;
  dupMode = (int)j.asInt(L"dupMode", dupMode);
  autoCleanDays = (int)j.asInt(L"autoCleanDays", autoCleanDays);
  maxImageMB = (int)j.asInt(L"maxImageMB", maxImageMB);
  pauseMonitoring =
      j.get(L"pauseMonitoring") ? j.get(L"pauseMonitoring")->asBool(pauseMonitoring) : pauseMonitoring;
  clearOnExit = j.get(L"clearOnExit") ? j.get(L"clearOnExit")->asBool(clearOnExit) : clearOnExit;
  themeMode = (int)j.asInt(L"themeMode", themeMode);
  compactRows = j.get(L"compactRows") ? j.get(L"compactRows")->asBool(compactRows) : compactRows;
  firstRun = j.get(L"firstRun") ? j.get(L"firstRun")->asBool(firstRun) : firstRun;

  if (const Json* ex = j.get(L"excludedApps"); ex && ex->t == Json::ARR) {
    excludedApps.clear();
    for (auto& v : ex->a)
      if (v.t == Json::STR) excludedApps.push_back(ToLowerW(v.s));
  }
  return true;
}

bool Settings::Save() const {
  Json j = Json::Obj();
  j.set(L"version", Json::I(1));
  Json hk = Json::Obj();
  hk.set(L"mod", Json::I((i64)hotkeyMods));
  hk.set(L"vk", Json::I((i64)hotkeyVk));
  j.set(L"hotkey", std::move(hk));
  j.set(L"autoPaste", Json::B(autoPaste));
  j.set(L"showTray", Json::B(showTray));
  j.set(L"startWithWindows", Json::B(startWithWindows));
  j.set(L"startInBackground", Json::B(startInBackground));
  j.set(L"maxItems", Json::I(maxItems));
  j.set(L"persist", Json::B(persist));
  j.set(L"dupMode", Json::I(dupMode));
  j.set(L"autoCleanDays", Json::I(autoCleanDays));
  j.set(L"maxImageMB", Json::I(maxImageMB));
  j.set(L"pauseMonitoring", Json::B(pauseMonitoring));
  j.set(L"clearOnExit", Json::B(clearOnExit));
  j.set(L"themeMode", Json::I(themeMode));
  j.set(L"compactRows", Json::B(compactRows));
  j.set(L"firstRun", Json::B(firstRun));
  Json arr = Json::Arr();
  for (auto& e : excludedApps) arr.a.push_back(Json::S(e));
  j.set(L"excludedApps", std::move(arr));
  string out = j.dump();
  return WriteAllBytesAtomic(ConfigPath(), out.data(), out.size());
}

void Settings::ApplyRunKey() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                    KEY_SET_VALUE, &k) != ERROR_SUCCESS)
    return;
  if (startWithWindows) {
    wstring exe = ExePath();
    wstring val = L"\"" + exe + L"\" /background";
    RegSetValueExW(k, L"ClipVault", 0, REG_SZ, (const BYTE*)val.c_str(),
                   (DWORD)((val.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(k, L"ClipVault");
  }
  RegCloseKey(k);
}

wstring HotkeyToString(UINT mods, UINT vk) {
  wstring s;
  wchar_t key[8] = {0};
  if (GetKeyNameTextW(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16, key, 8) <= 0 || !key[0])
    swprintf(key, 8, L"%C", (wchar_t)vk);
  if (mods & MOD_CONTROL) s += L"Ctrl+";
  if (mods & MOD_ALT) s += L"Alt+";
  if (mods & MOD_SHIFT) s += L"Shift+";
  if (mods & MOD_WIN) s += L"Win+";
  s += key;
  return s;
}

}  // namespace cv
