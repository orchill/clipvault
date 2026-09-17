#include "theme.h"
#include "settings.h"

#include <dwmapi.h>

namespace cv {

static COLORREF Rgb(int r, int g, int b) { return RGB(r, g, b); }

COLORREF Mix(COLORREF a, COLORREF b, int pctB) {
  int r = (int)GetRValue(a) + ((int)GetRValue(b) - (int)GetRValue(a)) * pctB / 100;
  int g = (int)GetGValue(a) + ((int)GetGValue(b) - (int)GetGValue(a)) * pctB / 100;
  int bl = (int)GetBValue(a) + ((int)GetBValue(b) - (int)GetBValue(a)) * pctB / 100;
  return RGB(r, g, bl);
}

static bool SystemPrefersLight() {
  // Light/Dark/System: System reads the OS "apps use light theme" toggle.
  DWORD v = 0, sz = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER,
                   L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                   L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS)
    return v != 0;
  return false;
}

static COLORREF SystemAccent() {
  // HKCU\Software\Microsoft\Windows\DWM\AccentColor is stored as 0x00BBGGRR.
  DWORD v = 0, sz = sizeof(v);
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor",
                   RRF_RT_REG_DWORD, nullptr, &v, &sz) == ERROR_SUCCESS) {
    COLORREF c = v & 0x00FFFFFF;
    if (c) return c;
  }
  return RGB(0x4C, 0xC2, 0xFF);
}

static Theme BuildTheme(int themeMode) {
  bool dark = themeMode == (int)ThemeMode::Dark ||
              (themeMode == (int)ThemeMode::System && !SystemPrefersLight());
  Theme t;
  t.dark = dark;
  if (dark) {
    t.bg = Rgb(0x20, 0x21, 0x24);
    t.surface = Rgb(0x2B, 0x2D, 0x31);
    t.hover = Rgb(0x33, 0x36, 0x3B);
    t.sel = Rgb(0x2F, 0x33, 0x39);
    t.stroke = Rgb(0x00, 0x81, 0xB4);
    t.text = Rgb(0xF2, 0xF3, 0xF5);
    t.textDim = Rgb(0x9C, 0xA1, 0xA8);
    t.accent = SystemAccent();
    t.chipActive = Mix(t.accent, t.bg, 82);
    t.chipIdle = Rgb(0x2B, 0x2D, 0x31);
    t.scrollbar = Rgb(0x6B, 0x70, 0x78);
    t.sep = Rgb(0x39, 0x3C, 0x41);
    t.placeholder = Rgb(0x76, 0x7B, 0x82);
  } else {
    t.bg = Rgb(0xF3, 0xF3, 0xF3);
    t.surface = Rgb(0xFF, 0xFF, 0xFF);
    t.hover = Rgb(0xEA, 0xEC, 0xEF);
    t.sel = Rgb(0xE2, 0xE8, 0xEF);
    t.stroke = Rgb(0x00, 0x67, 0xC0);
    t.text = Rgb(0x1A, 0x1A, 0x1A);
    t.textDim = Rgb(0x6B, 0x6F, 0x75);
    t.accent = SystemAccent();
    t.chipActive = Mix(t.accent, t.bg, 88);
    t.chipIdle = Rgb(0xE6, 0xE6, 0xE6);
    t.scrollbar = Rgb(0x9E, 0xA2, 0xA8);
    t.sep = Rgb(0xDC, 0xDC, 0xDC);
    t.placeholder = Rgb(0x8D, 0x92, 0x99);
  }
  return t;
}

static HFONT MakeFont(const wchar_t* face, int pt, int weight, int dpi, bool italic = false) {
  return CreateFontW(-MulDiv(pt, dpi, 72), 0, 0, 0, weight, italic ? TRUE : FALSE, FALSE,
                     FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face);
}

static bool GlyphAvailable(const wchar_t* face, wchar_t ch) {
  HFONT f = MakeFont(face, 10, FW_NORMAL, 96);
  if (!f) return false;
  HDC dc = GetDC(nullptr);
  HFONT old = (HFONT)SelectObject(dc, f);
  WORD idx = 0;
  bool ok = GetGlyphIndicesW(dc, &ch, 1, &idx, GGI_MARK_NONEXISTING_GLYPHS) == 1 && idx != 0xFFFF;
  SelectObject(dc, old);
  ReleaseDC(nullptr, dc);
  DeleteObject(f);
  return ok;
}

void UiCtx::ApplyDpi(HWND hwnd, int themeMode) {
  UINT dpiX = 96;
  HMODULE u32 = GetModuleHandleW(L"user32.dll");
  if (u32) {
    using Fn = UINT(WINAPI*)(HWND);
    if (Fn f = (Fn)GetProcAddress(u32, "GetDpiForWindow")) dpiX = f(hwnd);
  }
  if (!dpiX) {
    HDC dc = GetDC(hwnd);
    dpiX = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(hwnd, dc);
  }
  dpi = (int)dpiX;

  th = BuildTheme(themeMode);

  auto del = [](HFONT& f) {
    if (f) { DeleteObject(f); f = nullptr; }
  };
  del(hUi);
  del(hUiBold);
  del(hUiSmall);
  del(hIcon);

  hUi = MakeFont(L"Segoe UI", 9, FW_NORMAL, dpi);
  hUiBold = MakeFont(L"Segoe UI", 9, FW_SEMIBOLD, dpi);
  hUiSmall = MakeFont(L"Segoe UI", 8, FW_NORMAL, dpi);
  // Icon glyphs: prefer Segoe Fluent Icons (Win11), fall back to Segoe MDL2 Assets (Win10).
  const wchar_t* iconFace =
      GlyphAvailable(L"Segoe Fluent Icons", (wchar_t)GLYPH_SEARCH) ? L"Segoe Fluent Icons"
                                                                   : L"Segoe MDL2 Assets";
  hIcon = MakeFont(iconFace, 10, FW_NORMAL, dpi);

  rowH = S(56);
  rowHImage = S(64);
  pad = S(10);
  searchH = S(38);
  chipsH = S(30);
  footerH = S(32);
  thumb = S(46);
  width = S(432);
  height = S(560);
}

}  // namespace cv
