#include "settings_window.h"
#include "app.h"
#include "popup.h"

#include <objidl.h>
#include <gdiplus.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

namespace cv {

namespace {

constexpr wchar_t kClass[] = L"ClipVaultSettings";
constexpr int kW = 460;        // default client width, DIP
constexpr int kMinW = 440;     // minimum client width, DIP
constexpr int kMargin = 16;    // outer margin / card inset, DIP

enum CtrlId {
  ID_STARTWIN = 1000,
  ID_STARTBG,
  ID_TRAY,
  ID_HOTKEY,
  ID_AUTOPASTE,
  ID_MAXITEMS,
  ID_PERSIST,
  ID_DUP,
  ID_AUTOCLEAN,
  ID_PAUSE,
  ID_CLEAREXIT,
  ID_EXCLUDE,
  ID_THEME,
  ID_COMPACT,
  ID_CLEARUNPIN,
  ID_CLEARALL,
  ID_SAVE,
  ID_CANCEL,
};

// anchor modes for the layout table
enum { A_LEFT = 0, A_RIGHT = 1, A_STRETCH = 2, A_BOTTOM_L = 3, A_BOTTOM_R = 4 };

struct SettingsUi {
  HWND hwnd = nullptr;
  HWND hotkeyBtn = nullptr;
  bool capturing = false;
  HFONT font = nullptr, fontBold = nullptr;
  HBRUSH bgBrush = nullptr, surfaceBrush = nullptr;
  UiCtx* u = nullptr;
};

SettingsUi g_ui;

struct Pending {
  UINT mods = 0, vk = 0;
  bool hotkeyChanged = false;
};
Pending g_pending;

struct Section {
  const wchar_t* title;
  int top, bottom;  // DIP
};
std::vector<Section> g_sections;

// layout table: every child control, DIP coordinates, anchoring
struct Laid {
  HWND h;
  int xd, yd, wd, hd;
  int anchor;
};
std::vector<Laid> g_laid;
int g_contentH = 0;  // total content height, DIP

void RegisterLaid(HWND h, int xd, int yd, int wd, int hd, int anchor) {
  g_laid.push_back({h, xd, yd, wd, hd, anchor});
}

void Reposition() {
  if (!g_ui.hwnd || !g_ui.u) return;
  UiCtx& u = *g_ui.u;
  RECT rc;
  GetClientRect(g_ui.hwnd, &rc);
  int cw = rc.right, ch = rc.bottom;
  for (auto& L : g_laid) {
    int x, y, w, h;
    switch (L.anchor) {
      case A_RIGHT:
        x = cw - u.S(L.xd) - u.S(L.wd); y = u.S(L.yd); w = u.S(L.wd); h = u.S(L.hd); break;
      case A_STRETCH:
        x = u.S(L.xd); y = u.S(L.yd); w = cw - u.S(L.xd) - u.S(kMargin); h = u.S(L.hd); break;
      case A_BOTTOM_L:
        x = u.S(L.xd); y = ch - u.S(L.yd); w = u.S(L.wd); h = u.S(L.hd); break;
      case A_BOTTOM_R:
        x = cw - u.S(L.xd) - u.S(L.wd); y = ch - u.S(L.yd); w = u.S(L.wd); h = u.S(L.hd); break;
      default:
        x = u.S(L.xd); y = u.S(L.yd); w = u.S(L.wd); h = u.S(L.hd);
    }
    SetWindowPos(L.h, nullptr, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

// ---------------- control helpers ----------------

void SetCtrlTheme(HWND h, const wchar_t* theme = L"DarkMode_Explorer") {
  SetWindowTheme(h, theme, nullptr);
}

HWND AddCheck(HWND parent, int id, const wchar_t* label, int xd, int yd, int wd) {
  UiCtx& u = *g_ui.u;
  HWND h = CreateWindowExW(0, L"BUTTON", label,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, u.S(xd),
                           u.S(yd), u.S(wd), u.S(24), parent, (HMENU)(INT_PTR)id, nullptr,
                           nullptr);
  SendMessageW(h, WM_SETFONT, (WPARAM)g_ui.font, TRUE);
  SetCtrlTheme(h);
  RegisterLaid(h, xd, yd, wd, 24, A_LEFT);
  return h;
}

HWND AddLabel(HWND parent, const wchar_t* text, int xd, int yd, int wd, int hd, bool bold) {
  UiCtx& u = *g_ui.u;
  HWND h = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE, u.S(xd), u.S(yd),
                           u.S(wd), u.S(hd), parent, nullptr, nullptr, nullptr);
  SendMessageW(h, WM_SETFONT, (WPARAM)(bold ? g_ui.fontBold : g_ui.font), TRUE);
  RegisterLaid(h, xd, yd, wd, hd, A_LEFT);
  return h;
}

HWND AddCombo(HWND parent, int id, int xd, int yd, int wd) {
  UiCtx& u = *g_ui.u;
  HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"COMBOBOX", L"",
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                           u.S(xd), u.S(yd), u.S(wd), u.S(240), parent,
                           (HMENU)(INT_PTR)id, nullptr, nullptr);
  SendMessageW(h, WM_SETFONT, (WPARAM)g_ui.font, TRUE);
  SetCtrlTheme(h, L"DarkMode_CFD");
  RegisterLaid(h, xd, yd, wd, 24, A_LEFT);
  return h;
}

HWND AddButton(HWND parent, int id, const wchar_t* label, int xd, int yd, int wd, int hd,
               int anchor) {
  UiCtx& u = *g_ui.u;
  HWND h = CreateWindowExW(0, L"BUTTON", label,
                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON | BS_OWNERDRAW,
                           u.S(xd), u.S(yd), u.S(wd), u.S(hd), parent,
                           (HMENU)(INT_PTR)id, nullptr, nullptr);
  SendMessageW(h, WM_SETFONT, (WPARAM)g_ui.font, TRUE);
  RegisterLaid(h, xd, yd, wd, hd, anchor);
  return h;
}

void AddComboItem(HWND h, const wchar_t* text) { SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)text); }

int ComboSel(HWND h) { return (int)SendMessageW(h, CB_GETCURSEL, 0, 0); }
void ComboSetSel(HWND h, int idx) { SendMessageW(h, CB_SETCURSEL, idx, 0); }

// ---------------- owner-drawn buttons ----------------

void PaintButton(HWND hwnd, DRAWITEMSTRUCT* dis) {
  UiCtx& u = *g_ui.u;
  Theme& t = u.th;
  HDC dc = dis->hDC;
  RECT rc = dis->rcItem;
  int id = (int)dis->CtlID;
  bool pressed = (dis->itemState & ODS_SELECTED) != 0;
  bool hotkey = id == ID_HOTKEY;

  wchar_t text[128];
  GetWindowTextW(dis->hwndItem, text, 128);

  // parent-background fill over the whole item rect first, so the corners
  // outside the rounded path are correct regardless of the class brush
  HBRUSH bg = CreateSolidBrush(t.bg);
  FillRect(dc, &rc, bg);
  DeleteObject(bg);

  auto path = [&](int inset) {
    auto p = std::make_unique<Gdiplus::GraphicsPath>();
    int x = rc.left + inset, y = rc.top + inset;
    int w = rc.right - rc.left - inset * 2, h = rc.bottom - rc.top - inset * 2;
    int r = u.S(9);
    int d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    p->AddArc(x, y, d, d, 180, 90);
    p->AddArc(x + w - d, y, d, d, 270, 90);
    p->AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p->AddArc(x, y + h - d, d, d, 90, 90);
    p->CloseFigure();
    return p;
  };

  Gdiplus::Graphics g(dc);
  g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
  COLORREF fill, border, txt;
  if (hotkey) {
    fill = t.surface;
    border = g_ui.capturing ? t.accent : t.sep;
    txt = g_ui.capturing ? t.accent : t.text;
  } else if (id == ID_SAVE) {
    fill = pressed ? Mix(t.accent, t.bg, 25) : t.accent;
    border = fill;
    txt = t.dark ? RGB(0x10, 0x12, 0x18) : RGB(0xFF, 0xFF, 0xFF);
  } else {
    fill = pressed ? t.hover : t.surface;
    border = t.sep;
    txt = t.text;
  }

  auto p = path(0);
  Gdiplus::SolidBrush b(Gdiplus::Color(GetRValue(fill), GetGValue(fill), GetBValue(fill)));
  g.FillPath(&b, p.get());
  Gdiplus::Pen pen(Gdiplus::Color(GetRValue(border), GetGValue(border), GetBValue(border)),
                   hotkey && g_ui.capturing ? 2.0f : 1.0f);
  g.DrawPath(&pen, p.get());

  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, txt);
  HFONT old = (HFONT)SelectObject(dc, id == ID_SAVE ? g_ui.fontBold : g_ui.font);
  DrawTextW(dc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
  SelectObject(dc, old);
}

// ---------------- values ----------------

void LoadValues() {
  Settings& s = Settings::I();
  auto chk = [&](int id, bool v) {
    CheckDlgButton(g_ui.hwnd, id, v ? BST_CHECKED : BST_UNCHECKED);
  };
  chk(ID_STARTWIN, s.startWithWindows);
  chk(ID_STARTBG, s.startInBackground);
  chk(ID_TRAY, s.showTray);
  chk(ID_AUTOPASTE, s.autoPaste);
  chk(ID_PERSIST, s.persist);
  chk(ID_PAUSE, s.pauseMonitoring);
  chk(ID_CLEAREXIT, s.clearOnExit);
  chk(ID_COMPACT, s.compactRows);

  HWND cb;
  cb = GetDlgItem(g_ui.hwnd, ID_MAXITEMS);
  ComboSetSel(cb, s.maxItems <= 25 ? 0 : s.maxItems <= 50 ? 1 : s.maxItems <= 100 ? 2
                                                     : s.maxItems <= 200          ? 3
                                                                                  : 4);
  cb = GetDlgItem(g_ui.hwnd, ID_DUP);
  ComboSetSel(cb, s.dupMode);
  cb = GetDlgItem(g_ui.hwnd, ID_AUTOCLEAN);
  ComboSetSel(cb, s.autoCleanDays == 0 ? 0 : s.autoCleanDays <= 1 ? 1
                                                     : s.autoCleanDays <= 3 ? 2
                                                     : s.autoCleanDays <= 7 ? 3 : 4);
  cb = GetDlgItem(g_ui.hwnd, ID_THEME);
  ComboSetSel(cb, s.themeMode);

  SetWindowTextW(g_ui.hotkeyBtn, HotkeyToString(s.hotkeyMods, s.hotkeyVk).c_str());
  InvalidateRect(g_ui.hotkeyBtn, nullptr, TRUE);

  wstring excl;
  for (auto& e : s.excludedApps) {
    excl += e;
    excl += L"\r\n";
  }
  SetWindowTextW(GetDlgItem(g_ui.hwnd, ID_EXCLUDE), excl.c_str());
}

void SaveValues() {
  Settings& s = Settings::I();
  auto chk = [&](int id) { return IsDlgButtonChecked(g_ui.hwnd, id) == BST_CHECKED; };
  s.startWithWindows = chk(ID_STARTWIN);
  s.startInBackground = chk(ID_STARTBG);
  s.showTray = chk(ID_TRAY);
  s.autoPaste = chk(ID_AUTOPASTE);
  s.persist = chk(ID_PERSIST);
  s.pauseMonitoring = chk(ID_PAUSE);
  s.clearOnExit = chk(ID_CLEAREXIT);
  s.compactRows = chk(ID_COMPACT);

  static const int items[5] = {25, 50, 100, 200, 500};
  int sel = ComboSel(GetDlgItem(g_ui.hwnd, ID_MAXITEMS));
  if (sel >= 0 && sel < 5) s.maxItems = items[sel];
  sel = ComboSel(GetDlgItem(g_ui.hwnd, ID_DUP));
  if (sel >= 0) s.dupMode = sel;
  static const int days[5] = {0, 1, 3, 7, 30};
  sel = ComboSel(GetDlgItem(g_ui.hwnd, ID_AUTOCLEAN));
  if (sel >= 0 && sel < 5) s.autoCleanDays = days[sel];
  sel = ComboSel(GetDlgItem(g_ui.hwnd, ID_THEME));
  if (sel >= 0) s.themeMode = sel;

  if (g_pending.hotkeyChanged) {
    s.hotkeyMods = g_pending.mods;
    s.hotkeyVk = g_pending.vk;
  }

  wchar_t buf[4096];
  GetWindowTextW(GetDlgItem(g_ui.hwnd, ID_EXCLUDE), buf, 4096);
  s.excludedApps.clear();
  wchar_t* ctx = nullptr;
  wchar_t* tok = wcstok_s(buf, L"\r\n", &ctx);
  while (tok) {
    wstring t = ToLowerW(tok);
    size_t b = t.find_first_not_of(L" \t");
    if (b != wstring::npos) {
      size_t e = t.find_last_not_of(L" \t");
      s.excludedApps.push_back(t.substr(b, e - b + 1));
    }
    tok = wcstok_s(nullptr, L"\r\n", &ctx);
  }

  s.Save();
  s.ApplyRunKey();
  PostMessageW(A().hwndMain, WM_APP_SETTINGS_SAVED, 0, 0);
  DestroyWindow(g_ui.hwnd);
}

// ---------------- hotkey capture ----------------

LRESULT CALLBACK HotkeyBtnProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  switch (m) {
    case WM_KEYDOWN: {
      if (!g_ui.capturing) break;
      UINT vk = (UINT)w;
      if (vk == VK_ESCAPE) {
        g_ui.capturing = false;
        Settings& s = Settings::I();
        SetWindowTextW(h, HotkeyToString(s.hotkeyMods, s.hotkeyVk).c_str());
        InvalidateRect(h, nullptr, TRUE);
        return 0;
      }
      if (vk == VK_CONTROL || vk == VK_SHIFT || vk == VK_MENU || vk == VK_LWIN ||
          vk == VK_RWIN)
        return 0;
      UINT mods = 0;
      if (GetKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
      if (GetKeyState(VK_MENU) & 0x8000) mods |= MOD_ALT;
      if (GetKeyState(VK_SHIFT) & 0x8000) mods |= MOD_SHIFT;
      if (GetKeyState(VK_LWIN) & 0x8000 || GetKeyState(VK_RWIN) & 0x8000) mods |= MOD_WIN;
      if (!mods) return 0;  // require a modifier so the hotkey cannot eat typing
      g_pending.mods = mods;
      g_pending.vk = vk;
      g_pending.hotkeyChanged = true;
      g_ui.capturing = false;
      SetWindowTextW(h, HotkeyToString(mods, vk).c_str());
      InvalidateRect(h, nullptr, TRUE);
      return 0;
    }
    case WM_SETFOCUS: {
      if (g_ui.capturing) break;
      g_ui.capturing = true;
      SetWindowTextW(h, L"Press keys…  (Esc cancels)");
      InvalidateRect(h, nullptr, TRUE);
      DefSubclassProc(h, m, w, l);
      return 0;
    }
    case WM_KILLFOCUS: {
      if (g_ui.capturing) {
        g_ui.capturing = false;
        Settings& s = Settings::I();
        SetWindowTextW(h, HotkeyToString(s.hotkeyMods, s.hotkeyVk).c_str());
        InvalidateRect(h, nullptr, TRUE);
      }
      break;
    }
  }
  return DefSubclassProc(h, m, w, l);
}

// ---------------- window proc ----------------

LRESULT CALLBACK SettingsProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  switch (msg) {
    case WM_CREATE: {
      g_ui.hwnd = hwnd;
      UiCtx& u = A().ui;
      g_ui.u = &A().ui;
      g_ui.font = u.hUi;
      g_ui.fontBold = u.hUiBold;
      g_ui.bgBrush = CreateSolidBrush(u.th.bg);
      g_ui.surfaceBrush = CreateSolidBrush(u.th.surface);

      BOOL dark = u.th.dark ? TRUE : FALSE;
      DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
      DWORD pref = 2;
      DwmSetWindowAttribute(hwnd, 33, &pref, sizeof(pref));

      g_sections.clear();
      g_laid.clear();
      int y = 14;

      auto beginSection = [&](const wchar_t* title) {
        g_sections.push_back({title, y, 0});
        y += 30;
      };
      auto endSection = [&] {
        y += 8;
        g_sections.back().bottom = y;
        y += 14;
      };

      // ---- General ----
      beginSection(L"General");
      AddCheck(hwnd, ID_STARTWIN, L"Start with Windows", kMargin + 16, y, kW - 64); y += 27;
      AddCheck(hwnd, ID_STARTBG, L"Start minimized (background only)", kMargin + 16, y, kW - 64); y += 27;
      AddCheck(hwnd, ID_TRAY, L"Show system tray icon", kMargin + 16, y, kW - 64); y += 27;
      AddLabel(hwnd, L"Global shortcut:", kMargin + 16, y + 5, 120, 20, false);
      g_ui.hotkeyBtn = AddButton(hwnd, ID_HOTKEY, L"", kMargin + 150, y, 150, 30, A_LEFT);
      SetWindowSubclass(g_ui.hotkeyBtn, HotkeyBtnProc, 1, 0);
      AddCheck(hwnd, ID_AUTOPASTE, L"Paste immediately on selection (Ctrl+V)", kMargin + 16,
               y + 36, kW - 64);
      y += 64;
      endSection();

      // ---- History ----
      beginSection(L"History");
      AddLabel(hwnd, L"Maximum entries:", kMargin + 16, y + 5, 120, 20, false);
      HWND cb = AddCombo(hwnd, ID_MAXITEMS, kMargin + 150, y, 150);
      AddComboItem(cb, L"25 (default)");
      AddComboItem(cb, L"50");
      AddComboItem(cb, L"100");
      AddComboItem(cb, L"200");
      AddComboItem(cb, L"500");
      y += 30;
      AddCheck(hwnd, ID_PERSIST, L"Save clipboard history between restarts", kMargin + 16, y,
               kW - 64); y += 27;
      AddLabel(hwnd, L"Duplicate copies:", kMargin + 16, y + 5, 120, 20, false);
      cb = AddCombo(hwnd, ID_DUP, kMargin + 150, y, 240);
      AddComboItem(cb, L"Move existing to newest position");
      AddComboItem(cb, L"Keep existing in place");
      AddComboItem(cb, L"Allow duplicates");
      y += 30;
      AddLabel(hwnd, L"Automatic cleanup:", kMargin + 16, y + 5, 120, 20, false);
      cb = AddCombo(hwnd, ID_AUTOCLEAN, kMargin + 150, y, 240);
      AddComboItem(cb, L"Never");
      AddComboItem(cb, L"After 1 day");
      AddComboItem(cb, L"After 3 days");
      AddComboItem(cb, L"After 7 days");
      AddComboItem(cb, L"After 30 days");
      y += 32;
      endSection();

      // ---- Privacy ----
      beginSection(L"Privacy");
      AddCheck(hwnd, ID_PAUSE, L"Pause clipboard monitoring", kMargin + 16, y, kW - 64); y += 27;
      AddCheck(hwnd, ID_CLEAREXIT, L"Clear history on application exit", kMargin + 16, y,
               kW - 64); y += 27;
      // two-line label: full height reserved so nothing overlaps the edit below
      AddLabel(hwnd, L"Excluded apps — one process name per line. Nothing copied from these "
                     L"is recorded (e.g. keepass.exe):",
               kMargin + 16, y, kW - 64, 36, false);
      y += 40;
      HWND ex = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
                                    ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                u.S(kMargin + 16), u.S(y), u.S(kW - kMargin * 2 - 32), u.S(56),
                                hwnd, (HMENU)(INT_PTR)ID_EXCLUDE, nullptr, nullptr);
      SendMessageW(ex, WM_SETFONT, (WPARAM)g_ui.font, TRUE);
      SetCtrlTheme(ex, L"DarkMode_CFD");
      RegisterLaid(ex, kMargin + 16, y, kW - kMargin * 2 - 32, 56, A_STRETCH);
      y += 64;
      endSection();

      // ---- Appearance ----
      beginSection(L"Appearance");
      AddLabel(hwnd, L"Theme (light / dark / system):", kMargin + 16, y + 5, 190, 20, false);
      cb = AddCombo(hwnd, ID_THEME, kMargin + 210, y, 130);
      AddComboItem(cb, L"System");
      AddComboItem(cb, L"Dark");
      AddComboItem(cb, L"Light");
      y += 30;
      AddCheck(hwnd, ID_COMPACT, L"Compact item spacing", kMargin + 16, y, kW - 64); y += 27;
      endSection();

      // ---- action row (bottom anchored) ----
      y += 8;
      g_contentH = y + 44;
      AddButton(hwnd, ID_CLEARUNPIN, L"Clear unpinned", kMargin, 44, 122, 32, A_BOTTOM_L);
      AddButton(hwnd, ID_CLEARALL, L"Clear all", kMargin + 132, 44, 96, 32, A_BOTTOM_L);
      AddButton(hwnd, ID_CANCEL, L"Cancel", 120, 44, 96, 32, A_BOTTOM_R);
      AddButton(hwnd, ID_SAVE, L"Save", 16, 44, 96, 32, A_BOTTOM_R);

      RECT wr{0, 0, u.S(kW), u.S(g_contentH)};
      AdjustWindowRectEx(&wr,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                         FALSE, 0);
      SetWindowPos(hwnd, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top,
                   SWP_NOMOVE | SWP_NOZORDER);

      LoadValues();
      Reposition();
      return 0;
    }

    case WM_GETMINMAXINFO: {
      // never allow shrinking below the laid-out content
      UiCtx& u = A().ui;
      auto* mmi = (MINMAXINFO*)lParam;
      RECT wr{0, 0, u.S(kMinW), u.S(g_contentH)};
      AdjustWindowRectEx(&wr,
                         WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
                         FALSE, 0);
      mmi->ptMinTrackSize.x = wr.right - wr.left;
      mmi->ptMinTrackSize.y = wr.bottom - wr.top;
      return 0;
    }

    case WM_SIZE:
      Reposition();
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;

    case WM_ERASEBKGND:
      return TRUE;  // fully custom-painted: background is drawn in WM_PAINT

    case WM_PAINT: {
      // full repaint every time: background + section cards + headers.
      // (relying on WM_ERASEBKGND alone tiled fragments across resized areas)
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      UiCtx& u = *g_ui.u;
      Theme& t = u.th;
      RECT rc;
      GetClientRect(hwnd, &rc);
      HBRUSH bg = CreateSolidBrush(t.bg);
      FillRect(dc, &rc, bg);
      DeleteObject(bg);
      SetBkMode(dc, TRANSPARENT);
      for (auto& sec : g_sections) {
        auto path = std::make_unique<Gdiplus::GraphicsPath>();
        int x = u.S(kMargin);
        int w = rc.right - 2 * u.S(kMargin);
        int y0 = u.S(sec.top), h = u.S(sec.bottom - sec.top);
        int r = u.S(9);
        int d = r * 2;
        if (d > w) d = w;
        if (d > h) d = h;
        path->AddArc(x, y0, d, d, 180, 90);
        path->AddArc(x + w - d, y0, d, d, 270, 90);
        path->AddArc(x + w - d, y0 + h - d, d, d, 0, 90);
        path->AddArc(x, y0 + h - d, d, d, 90, 90);
        path->CloseFigure();
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(GetRValue(t.sep), GetGValue(t.sep), GetBValue(t.sep)));
        g.DrawPath(&pen, path.get());
        HFONT old = (HFONT)SelectObject(dc, g_ui.fontBold);
        SetTextColor(dc, t.accent);
        RECT rcH = {x + u.S(14), y0 + u.S(7), x + w, y0 + u.S(27)};
        DrawTextW(dc, sec.title, -1, &rcH, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, old);
      }
      EndPaint(hwnd, &ps);
      return 0;
    }

    case WM_DRAWITEM:
      PaintButton(hwnd, (DRAWITEMSTRUCT*)lParam);
      return TRUE;

    case WM_COMMAND: {
      int id = LOWORD(wParam);
      switch (id) {
        case ID_SAVE:
          SaveValues();
          return 0;
        case ID_CANCEL:
          DestroyWindow(hwnd);
          return 0;
        case ID_HOTKEY:
          SetFocus(g_ui.hotkeyBtn);
          return 0;
        case ID_CLEARUNPIN:
          A().history.ClearUnpinned();
          A().SchedulePersist();
          PopupRefresh();
          return 0;
        case ID_CLEARALL:
          A().history.ClearAll();
          A().SchedulePersist();
          PopupRefresh();
          return 0;
      }
      break;
    }

    case WM_CTLCOLORSTATIC: {
      HDC dc = (HDC)wParam;
      SetBkColor(dc, A().ui.th.bg);
      SetTextColor(dc, A().ui.th.text);
      return (LRESULT)g_ui.bgBrush;
    }
    case WM_CTLCOLORBTN: {
      // corners of owner-drawn buttons erase with the parent background
      return (LRESULT)g_ui.bgBrush;
    }
    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wParam;
      SetBkColor(dc, A().ui.th.surface);
      SetTextColor(dc, A().ui.th.text);
      return (LRESULT)g_ui.surfaceBrush;
    }
    case WM_CTLCOLORLISTBOX: {
      HDC dc = (HDC)wParam;
      SetBkColor(dc, A().ui.th.surface);
      SetTextColor(dc, A().ui.th.text);
      return (LRESULT)g_ui.surfaceBrush;
    }

    case WM_CLOSE:
      DestroyWindow(hwnd);
      return 0;

    case WM_DESTROY:
      g_ui.hwnd = nullptr;
      if (g_ui.bgBrush) { DeleteObject(g_ui.bgBrush); g_ui.bgBrush = nullptr; }
      if (g_ui.surfaceBrush) { DeleteObject(g_ui.surfaceBrush); g_ui.surfaceBrush = nullptr; }
      return 0;

    case WM_SETCURSOR:
      SetCursor(LoadCursorW(nullptr, IDC_ARROW));
      return TRUE;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

bool IsSettingsWindowOpen() { return g_ui.hwnd != nullptr; }

void OpenSettingsWindow() {
  if (g_ui.hwnd) {
    SetForegroundWindow(g_ui.hwnd);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SettingsProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    wc.hbrBackground = nullptr;
    wc.hIcon = A().hIconApp;
    wc.hIconSm = A().hIconApp;
    RegisterClassExW(&wc);
    registered = true;
  }
  UiCtx& u = A().ui;
  HWND hOwner = A().hwndMain;
  g_ui.hwnd = CreateWindowExW(
      0, kClass, L"ClipVault Settings",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX, CW_USEDEFAULT,
      CW_USEDEFAULT, u.S(kW), u.S(700), hOwner, nullptr, GetModuleHandleW(nullptr), nullptr);
  if (g_ui.hwnd) {
    ShowWindow(g_ui.hwnd, SW_SHOW);
    SetForegroundWindow(g_ui.hwnd);
  }
}

}  // namespace cv
