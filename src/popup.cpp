#include "popup.h"
#include "settings_window.h"

#include <commctrl.h>
#include <dwmapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <windowsx.h>
#include <shellapi.h>




namespace cv {

namespace {

constexpr wchar_t kClass[] = L"ClipVaultPopup";

// context menu commands
enum { CM_ACTIVATE = 2001, CM_PIN = 2002, CM_DELETE = 2003, CM_COPY = 2004, CM_OPENPHOTO = 2005, CM_OPENLOC = 2006, CM_OPENWITH = 2007 };

struct PopupState {
  std::vector<Item*> view;
  int sel = 0;
  int scroll = 0;  // first visible row
  int hover = -1;  // view index under cursor, -1 none
  int hoverBtn = 0;  // 0 none, 1 pin, 2 delete
  int chip = 0;    // 0 All, 1 Text, 2 Images, 3 Pinned
  wstring search, searchLower;
  RECT rcSearch{}, rcChips{}, rcList{}, rcFooter{}, rcGear{};
  int rowH = 0;
  bool dragScroll = false;
  int dragY0 = 0, dragScroll0 = 0;
  bool trackMouse = false;
  int visibleRows = 1;
  int totalRowsPx = 0;
};

PopupState* S() {
  if (!A().popup) A().popup = new PopupState();
  return (PopupState*)A().popup;
}

// ---------------- gdi+ helpers ----------------
Gdiplus::Color Gcol(COLORREF c, int a = 255) {
  return Gdiplus::Color((BYTE)a, GetRValue(c), GetGValue(c), GetBValue(c));
}

std::unique_ptr<Gdiplus::GraphicsPath> RoundPath(int x, int y, int w, int h, int r) {
  auto p = std::make_unique<Gdiplus::GraphicsPath>();
  if (w <= 0 || h <= 0) return p;
  int d = r * 2;
  if (d > w) d = w;
  if (d > h) d = h;
  p->AddArc(x, y, d, d, 180, 90);
  p->AddArc(x + w - d, y, d, d, 270, 90);
  p->AddArc(x + w - d, y + h - d, d, d, 0, 90);
  p->AddArc(x, y + h - d, d, d, 90, 90);
  p->CloseFigure();
  return p;
}

void FillRound(Gdiplus::Graphics& g, int x, int y, int w, int h, int r, COLORREF c, int a = 255) {
  auto p = RoundPath(x, y, w, h, r);
  Gdiplus::SolidBrush b(Gcol(c, a));
  g.FillPath(&b, p.get());
}

// first line of text, control chars flattened (emoji/surrogates preserved as-is)
wstring PreviewText(const Item& it) {
  if (it.type == ItemType::Image) {
    wchar_t buf[96];
    swprintf(buf, 96, L"Image %d×%d", it.imgW, it.imgH);
    return buf;
  }
  wstring out;
  out.reserve(it.text.size() < 512 ? it.text.size() : 512);
  for (wchar_t c : it.text) {
    if (c == L'\r') continue;
    if (c == L'\n' || c == L'\t') c = L' ';
    out += c;
    if (out.size() >= 512) break;
  }
  return out;
}

wstring MetaText(const Item& it) {
  wstring m;
  if (it.type != ItemType::Text) m += ItemTypeName(it.type);
  if (!it.srcApp.empty()) {
    if (!m.empty()) m += L" · ";
    m += it.srcApp;
  }
  if (!m.empty()) m += L" · ";
  m += RelativeTime(it.ts);
  m += L" · ";
  m += FormatBytes(it.DisplaySize());
  return m;
}

wchar_t TypeGlyph(const Item& it) {
  switch (it.type) {
    case ItemType::Image: return (wchar_t)GLYPH_PICTURE;
    case ItemType::Html: return (wchar_t)GLYPH_CODE;
    case ItemType::Files: return (wchar_t)GLYPH_FOLDER;
    case ItemType::Rtf: return (wchar_t)GLYPH_DOC;
    default: return (wchar_t)GLYPH_DOC;
  }
}

// ---------------- layout ----------------
void Layout(HWND hwnd) {
  App& a = A();
  PopupState& s = *S();
  UiCtx& u = a.ui;
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right, h = rc.bottom;

  s.rowH = Settings::I().compactRows ? u.S(44) : u.S(58);
  s.rcSearch = {u.pad, u.pad, w - u.pad, u.pad + u.searchH};
  int y = u.pad + u.searchH + u.S(8);
  s.rcChips = {u.pad, y, w - u.pad, y + u.chipsH};
  y += u.chipsH + u.S(8);
  s.rcFooter = {0, h - u.footerH, w, h};
  s.rcList = {0, y, w, h - u.footerH};
  s.rcGear = {w - u.pad - u.S(26), s.rcFooter.top + (u.footerH - u.S(26)) / 2,
              w - u.pad, s.rcFooter.top + (u.footerH + u.S(26)) / 2};
  s.visibleRows = s.rowH ? (s.rcList.bottom - s.rcList.top) / s.rowH : 1;

  // search edit sits inside the search card, right of the magnifier glyph
  int glyphW = u.S(30);
  int editH = u.searchH - u.S(10);
  MoveWindow(a.hwndEdit, s.rcSearch.left + glyphW, s.rcSearch.top + u.S(5),
             s.rcSearch.right - s.rcSearch.left - glyphW - u.S(10), editH, TRUE);
}

void EnsureVisible(PopupState& s) {
  if (s.sel < 0) s.sel = 0;
  if (s.sel >= (int)s.view.size()) s.sel = (int)s.view.size() - 1;
  if (s.sel < s.scroll) s.scroll = s.sel;
  if (s.sel >= s.scroll + s.visibleRows) s.scroll = s.sel - s.visibleRows + 1;
  int maxScroll = (int)s.view.size() - s.visibleRows;
  if (maxScroll < 0) maxScroll = 0;
  if (s.scroll > maxScroll) s.scroll = maxScroll;
  if (s.scroll < 0) s.scroll = 0;
}

void HidePreview();  // defined with the preview window below

void RebuildView() {
  App& a = A();
  PopupState& s = *S();
  HidePreview();  // view is about to change: rows shift under the cursor
  a.history.BuildView(s.searchLower, s.chip, s.view);
  EnsureVisible(s);
}

// popup height that fits the current result count (capped at the default)
int PopupDesiredHeight() {
  App& a = A();
  PopupState& s = *S();
  UiCtx& u = a.ui;
  int top = u.pad + u.searchH + u.S(8) + u.chipsH + u.S(8);
  int rows = (int)s.view.size();
  int maxList = u.height - top - u.footerH;
  int listH = rows ? rows * s.rowH + u.S(4) : u.S(110);
  if (listH > maxList) listH = maxList;
  return top + listH + u.footerH;
}

void PopupUpdateHeight(HWND hwnd) {
  RECT rc;
  GetClientRect(hwnd, &rc);
  int want = PopupDesiredHeight();
  if (rc.bottom != want) {
    RECT wr;
    GetWindowRect(hwnd, &wr);
    SetWindowPos(hwnd, nullptr, wr.left, wr.top, wr.right - wr.left, want,
                 SWP_NOZORDER | SWP_NOACTIVATE);
  }
}

int MaxScroll(PopupState& s) {
  int m = (int)s.view.size() - s.visibleRows;
  return m < 0 ? 0 : m;
}

// ---------------- painting ----------------
void DrawGlyph(HDC dc, int x, int y, int w, int h, wchar_t glyph, COLORREF color, HFONT font) {
  HFONT old = (HFONT)SelectObject(dc, font);
  SetTextColor(dc, color);
  RECT rc = {x, y, x + w, y + h};
  DrawTextW(dc, &glyph, 1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);
}

void DrawRow(HDC dc, Gdiplus::Graphics& g, PopupState& s, int idx, const RECT& rcRow) {
  App& a = A();
  UiCtx& u = a.ui;
  Item* it = s.view[idx];
  Theme& t = u.th;

  bool selected = idx == s.sel;
  bool hovered = idx == s.hover;
  if (selected || hovered) {
    RECT card = {rcRow.left + u.S(6), rcRow.top + u.S(1), rcRow.right - u.S(6),
                 rcRow.bottom - u.S(1)};
    auto path = RoundPath(card.left, card.top, card.right - card.left,
                          card.bottom - card.top, u.S(8));
    Gdiplus::SolidBrush b(Gcol(selected ? t.sel : t.hover));
    g.FillPath(&b, path.get());
    if (selected) {
      Gdiplus::Pen p(Gcol(t.stroke), 1.0f);
      g.DrawPath(&p, path.get());
    }
  }

  // icon / thumbnail box
  int box = u.S(34);
  int bx = rcRow.left + u.S(12);
  int by = rcRow.top + ((rcRow.bottom - rcRow.top) - box) / 2;
  if (it->type == ItemType::Image && it->thumb) {
    auto clip = RoundPath(bx, by, box, box, u.S(6));
    Gdiplus::Region rgn(clip.get());
    g.SetClip(&rgn);
    g.DrawImage((Gdiplus::Bitmap*)it->thumb, bx, by, box, box);
    g.ResetClip();
    auto stroke = RoundPath(bx, by, box, box, u.S(6));
    Gdiplus::Pen pen(Gcol(t.sep));
    g.DrawPath(&pen, stroke.get());
  } else {
    DrawGlyph(dc, bx, by, box, box, TypeGlyph(*it), t.textDim, u.hIcon);
  }

  // text block
  int tx = bx + box + u.S(10);
  int tw = rcRow.right - tx - u.S(74);  // room for pin/delete buttons
  COLORREF lineColor = t.text;
  COLORREF metaColor = t.textDim;
  if (selected) lineColor = t.text;

  RECT rcLine1 = {tx, rcRow.top + u.S(6), tx + tw, rcRow.top + u.S(6) + u.S(18)};
  RECT rcLine2 = {tx, rcRow.bottom - u.S(6) - u.S(15), tx + tw, rcRow.bottom - u.S(6)};

  HFONT old = (HFONT)SelectObject(dc, u.hUi);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, lineColor);
  wstring p1 = PreviewText(*it);
  if (p1.empty()) p1 = L"(empty)";
  DrawTextW(dc, p1.c_str(), (int)p1.size(), &rcLine1,
            DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);

  SelectObject(dc, u.hUiSmall);
  SetTextColor(dc, metaColor);
  wstring p2 = MetaText(*it);
  DrawTextW(dc, p2.c_str(), (int)p2.size(), &rcLine2,
            DT_LEFT | DT_END_ELLIPSIS | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, old);

  // right-side buttons: pin (accent when pinned), delete (hover only)
  int btn = u.S(26);
  int delX = rcRow.right - u.S(10) - btn;
  int pinX = delX - btn - u.S(2);
  int btnY = rcRow.top + ((rcRow.bottom - rcRow.top) - btn) / 2;
  bool showButtons = hovered && idx == s.hover;
  if (it->pinned || showButtons) {
    DrawGlyph(dc, pinX, btnY, btn, btn, it->pinned ? (wchar_t)GLYPH_PIN : (wchar_t)GLYPH_UNPIN,
              it->pinned ? t.accent : t.textDim, u.hIcon);
  }
  if (showButtons) {
    COLORREF delColor = s.hoverBtn == 2 ? RGB(0xE8, 0x6C, 0x6C) : t.textDim;
    DrawGlyph(dc, delX, btnY, btn, btn, (wchar_t)GLYPH_DELETE, delColor, u.hIcon);
  }
}

void Paint(HWND hwnd) {
  App& a = A();
  PopupState& s = *S();
  UiCtx& u = a.ui;
  Theme& t = u.th;
  PAINTSTRUCT ps;
  HDC wdc = BeginPaint(hwnd, &ps);
  RECT rc;
  GetClientRect(hwnd, &rc);
  int w = rc.right, h = rc.bottom;

  HDC dc = CreateCompatibleDC(wdc);
  HBITMAP bmp = CreateCompatibleBitmap(wdc, w, h);
  HGDIOBJ oldBmp = SelectObject(dc, bmp);

  // background + rounded window border
  HBRUSH bg = CreateSolidBrush(t.bg);
  FillRect(dc, &rc, bg);
  DeleteObject(bg);
  SetBkMode(dc, TRANSPARENT);
  {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    auto border = RoundPath(0, 0, w - 1, h - 1, u.S(10));
    Gdiplus::Pen pen(Gcol(t.sep));
    g.DrawPath(&pen, border.get());

    // search card
    FillRound(g, s.rcSearch.left, s.rcSearch.top, s.rcSearch.right - s.rcSearch.left,
              s.rcSearch.bottom - s.rcSearch.top, u.S(8), t.surface);
    // search glyph
    DrawGlyph(dc, s.rcSearch.left, s.rcSearch.top, u.S(30),
              s.rcSearch.bottom - s.rcSearch.top, (wchar_t)GLYPH_SEARCH, t.placeholder, u.hIcon);

    // chips
    struct ChipDef {
      const wchar_t* label;
    };
    ChipDef chips[4] = {{L"All"}, {L"Text"}, {L"Images"}, {L"Pinned"}};
    int cx = s.rcChips.left;
    for (int k = 0; k < 4; k++) {
      HFONT old = (HFONT)SelectObject(dc, u.hUi);
      SIZE sz;
      GetTextExtentPoint32W(dc, chips[k].label, (int)wcslen(chips[k].label), &sz);
      SelectObject(dc, old);
      int cw = sz.cx + u.S(22);
      int chh = s.rcChips.bottom - s.rcChips.top - u.S(2);
      int cy = s.rcChips.top + u.S(1);
      bool active = k == s.chip;
      FillRound(g, cx, cy, cw, chh, chh / 2, active ? t.chipActive : t.chipIdle);
      HFONT of = (HFONT)SelectObject(dc, active ? u.hUiBold : u.hUi);
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, active ? t.text : t.textDim);
      RECT rcTxt = {cx, cy, cx + cw, cy + chh};
      DrawTextW(dc, chips[k].label, -1, &rcTxt, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
      SelectObject(dc, of);
      cx += cw + u.S(6);
    }
  }

  // rows
  if (s.view.empty()) {
    // empty state
    const wchar_t* title;
    const wchar_t* sub = L"";
    if (!s.searchLower.empty()) {
      title = L"No matches";
      sub = L"Try a different search.";
    } else if (a.history.TotalCount() == 0) {
      title = L"No clipboard history yet";
      sub = L"Copy anything, then press Ctrl+Shift+V.";
    } else {
      title = L"Nothing here";
      sub = L"No entries match this filter.";
    }
    SetBkMode(dc, TRANSPARENT);
    DrawGlyph(dc, 0, s.rcList.top + (s.rcList.bottom - s.rcList.top) / 2 - u.S(52), w, u.S(40),
              (wchar_t)GLYPH_HISTORY, t.sep, u.hIcon);
    HFONT old = (HFONT)SelectObject(dc, u.hUiBold);
    SetTextColor(dc, t.textDim);
    RECT rc1 = {0, s.rcList.top + (s.rcList.bottom - s.rcList.top) / 2 - u.S(6), w,
                s.rcList.top + (s.rcList.bottom - s.rcList.top) / 2 + u.S(14)};
    DrawTextW(dc, title, -1, &rc1, DT_CENTER | DT_SINGLELINE);
    SelectObject(dc, u.hUiSmall);
    RECT rc2 = {0, rc1.bottom + u.S(2), w, rc1.bottom + u.S(22)};
    DrawTextW(dc, sub, -1, &rc2, DT_CENTER | DT_SINGLELINE);
    SelectObject(dc, old);
  } else {
    int first = s.scroll;
    int last = s.scroll + s.visibleRows + 1;
    if (last > (int)s.view.size()) last = (int)s.view.size();
    for (int idx = first; idx < last; idx++) {
      RECT rcRow = {s.rcList.left, s.rcList.top + (idx - first) * s.rowH, s.rcList.right,
                    s.rcList.top + (idx - first + 1) * s.rowH};
      // separator between pinned and unpinned sections
      if (idx > 0 && s.view[idx]->pinned != s.view[idx - 1]->pinned) {
        Gdiplus::Graphics g(dc);
        Gdiplus::Pen pen(Gcol(t.sep));
        g.DrawLine(&pen, (Gdiplus::REAL)(rcRow.left + u.S(14)), (Gdiplus::REAL)rcRow.top,
                   (Gdiplus::REAL)(rcRow.right - u.S(14)), (Gdiplus::REAL)rcRow.top);
      }
      Gdiplus::Graphics g(dc);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      DrawRow(dc, g, s, idx, rcRow);
    }

    // overlay scrollbar
    s.totalRowsPx = (int)s.view.size() * s.rowH;
    int listH = s.rcList.bottom - s.rcList.top;
    if (s.totalRowsPx > listH) {
      int track = listH - u.S(8);
      int thumbH = track * listH / s.totalRowsPx;
      if (thumbH < u.S(24)) thumbH = u.S(24);
      int maxScroll = MaxScroll(s);
      int ty = s.rcList.top + u.S(4);
      if (maxScroll > 0) ty += (track - thumbH) * s.scroll / maxScroll;
      Gdiplus::Graphics g(dc);
      FillRound(g, s.rcList.right - u.S(6), ty, u.S(4), thumbH, u.S(2), t.scrollbar, 140);
    }
  }

  // footer
  {
    Gdiplus::Graphics g(dc);
    Gdiplus::Pen pen(Gcol(t.sep));
    g.DrawLine(&pen, 0, s.rcFooter.top, w, s.rcFooter.top);
    SetBkMode(dc, TRANSPARENT);
    HFONT old = (HFONT)SelectObject(dc, u.hUiSmall);
    Settings& st = Settings::I();
    wstring hint = st.pauseMonitoring
                       ? L"Monitoring paused — new copies are not recorded"
                       : L"Enter paste · Del delete · Ctrl+P pin · Esc close";
    SetTextColor(dc, st.pauseMonitoring ? t.accent : t.textDim);
    RECT rcH = {u.pad, s.rcFooter.top, s.rcGear.left - u.S(8), s.rcFooter.bottom};
    DrawTextW(dc, hint.c_str(), (int)hint.size(), &rcH,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    DrawGlyph(dc, s.rcGear.left, s.rcGear.top, s.rcGear.right - s.rcGear.left,
              s.rcGear.bottom - s.rcGear.top, (wchar_t)GLYPH_GEAR, t.textDim, u.hIcon);
  }

  BitBlt(wdc, 0, 0, w, h, dc, 0, 0, SRCCOPY);
  SelectObject(dc, oldBmp);
  DeleteObject(bmp);
  DeleteDC(dc);
  EndPaint(hwnd, &ps);
}

// ---------------- hit testing ----------------
int RowAtY(PopupState& s, int y) {
  if (y < s.rcList.top || y >= s.rcList.bottom) return -1;
  int idx = s.scroll + (y - s.rcList.top) / s.rowH;
  if (idx >= (int)s.view.size()) return -1;
  return idx;
}

RECT RowRect(PopupState& s, int idx) {
  int y = s.rcList.top + (idx - s.scroll) * s.rowH;
  return {s.rcList.left, y, s.rcList.right, y + s.rowH};
}

int PinBtnRect(PopupState& s, int idx, RECT* out) {
  RECT rcRow = RowRect(s, idx);
  UiCtx& u = A().ui;
  int btn = u.S(26);
  int delX = rcRow.right - u.S(10) - btn;
  int pinX = delX - btn - u.S(2);
  int btnY = rcRow.top + ((rcRow.bottom - rcRow.top) - btn) / 2;
  *out = {pinX, btnY, pinX + btn, btnY + btn};
  return 1;
}

int DelBtnRect(PopupState& s, int idx, RECT* out) {
  RECT rcRow = RowRect(s, idx);
  UiCtx& u = A().ui;
  int btn = u.S(26);
  int delX = rcRow.right - u.S(10) - btn;
  int btnY = rcRow.top + ((rcRow.bottom - rcRow.top) - btn) / 2;
  *out = {delX, btnY, delX + btn, btnY + btn};
  return 1;
}

bool InRect(const RECT& r, int x, int y) {
  return x >= r.left && x < r.right && y >= r.top && y < r.bottom;
}

// ---------------- large image hover preview ----------------

bool g_openWithDialogOpen = false;            // suppress auto-hide while the app picker is up
HWND g_hwndPreview = nullptr;                 // borderless topmost preview window
std::unique_ptr<Gdiplus::Bitmap> g_previewBmp;  // decoded full image, only while shown
int g_previewRow = -1;                        // view index being previewed / pending
i64 g_previewId = 0;                          // item id the pending preview is for

LRESULT CALLBACK PreviewProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  switch (msg) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwnd, &ps);
      RECT rc;
      GetClientRect(hwnd, &rc);
      Theme& t = A().ui.th;
      HBRUSH b = CreateSolidBrush(t.surface);
      FillRect(dc, &rc, b);
      DeleteObject(b);
      Gdiplus::Graphics g(dc);
      g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
      g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
      if (g_previewBmp) {
        UINT iw = g_previewBmp->GetWidth(), ih = g_previewBmp->GetHeight();
        if (iw && ih) {
          int bw = rc.right - 2, bh = rc.bottom - 2;
          double scale = ((double)bw / iw < (double)bh / ih) ? (double)bw / iw : (double)bh / ih;
          if (scale > 2.0) scale = 2.0;  // small images: don't upscale into a blur
          int dw = (int)(iw * scale), dh = (int)(ih * scale);
          Gdiplus::Rect dst((rc.right - dw) / 2, (rc.bottom - dh) / 2, dw, dh);
          g.DrawImage(g_previewBmp.get(), dst, 0, 0, (int)iw, (int)ih, Gdiplus::UnitPixel);
        }
      }
      Gdiplus::Pen pen(Gcol(t.sep));
      g.DrawRectangle(&pen, 0, 0, rc.right - 1, rc.bottom - 1);
      EndPaint(hwnd, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    default:
      return DefWindowProcW(hwnd, msg, w, l);
  }
}

void HidePreview() {
  App& a = A();
  if (a.hwndMain) KillTimer(a.hwndMain, TIMER_PREVIEW);
  g_previewRow = -1;
  g_previewId = 0;
  if (g_hwndPreview && IsWindowVisible(g_hwndPreview)) ShowWindow(g_hwndPreview, SW_HIDE);
  g_previewBmp.reset();
}

void ShowPreview(PopupState& s, Item* it, int row) {
  App& a = A();
  UiCtx& u = a.ui;
  if (!it || it->imgFile.empty()) return;
  wstring path = BlobDir() + L"\\" + Utf8ToUtf16(it->imgFile);
  std::unique_ptr<Gdiplus::Bitmap> src(new Gdiplus::Bitmap(path.c_str()));

  if (!g_hwndPreview) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = PreviewProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = L"ClipVaultPreview";
    RegisterClassExW(&wc);
    // LAYERED + TRANSPARENT: mouse input passes through, so the popup beneath
    // keeps working even when a large preview overlaps it
    g_hwndPreview = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED | WS_EX_TRANSPARENT,
        L"ClipVaultPreview", L"", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (g_hwndPreview) {
      DWORD pref = 2;
      DwmSetWindowAttribute(g_hwndPreview, 33, &pref, sizeof(pref));
      SetLayeredWindowAttributes(g_hwndPreview, 0, 255, LWA_ALPHA);
    }
  }

  UINT iw = src->GetWidth(), ih = src->GetHeight();
  if (!iw || !ih) return;

  // size: proportional to the monitor work area so full-desktop captures show
  // large, clamped so nothing leaves the screen; small images upscale max 2x
  RECT pr;
  GetWindowRect(a.hwndMain, &pr);
  HMONITOR mon = MonitorFromWindow(a.hwndMain, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi{};
  mi.cbSize = sizeof(mi);
  GetMonitorInfoW(mon, &mi);
  int workW = mi.rcWork.right - mi.rcWork.left;
  int workH = mi.rcWork.bottom - mi.rcWork.top;
  int maxW = workW * 55 / 100, maxH = workH * 70 / 100;
  double scale = ((double)maxW / iw < (double)maxH / ih) ? (double)maxW / iw : (double)maxH / ih;
  if (scale > 2.0) scale = 2.0;
  int w = (int)(iw * scale), h = (int)(ih * scale);

  // position: beside the popup when it fits, otherwise centered over the work
  // area (the preview is click-through, so the popup keeps working beneath it)
  int x;
  if (pr.right + u.S(10) + w <= mi.rcWork.right)
    x = pr.right + u.S(10);
  else if (pr.left - u.S(10) - w >= mi.rcWork.left)
    x = pr.left - u.S(10) - w;
  else
    x = mi.rcWork.left + (workW - w) / 2;
  RECT rr = RowRect(s, row);
  POINT pt{rr.left, (rr.top + rr.bottom) / 2};
  ClientToScreen(a.hwndMain, &pt);
  int y = pt.y - h / 2;
  if (y < mi.rcWork.top) y = mi.rcWork.top;
  if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;

  g_previewBmp = std::move(src);
  SetWindowPos(g_hwndPreview, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
  InvalidateRect(g_hwndPreview, nullptr, FALSE);
}

void UpdatePreviewIntent(HWND hwnd, int row) {
  PopupState& s = *S();
  Settings& st = Settings::I();
  bool eligible = st.hoverPreview && row >= 0 && row < (int)s.view.size() &&
                  s.view[row]->type == ItemType::Image && !s.view[row]->imgFile.empty();
  if (!eligible) {
    if (g_previewRow != -1) HidePreview();
    return;
  }
  if (row == g_previewRow) return;  // already pending or showing for this row
  HidePreview();
  g_previewRow = row;
  g_previewId = s.view[row]->id;
  SetTimer(hwnd, TIMER_PREVIEW, 1500, nullptr);
}

void PreviewTimerFired(HWND hwnd) {
  KillTimer(hwnd, TIMER_PREVIEW);
  PopupState& s = *S();
  if (g_previewRow < 0 || g_previewRow >= (int)s.view.size()) { HidePreview(); return; }
  Item* it = s.view[g_previewRow];
  ShowPreview(s, it, g_previewRow);
}

// returns chip index or -1
int ChipAt(PopupState& s, int x) {
  UiCtx& u = A().ui;
  HDC dc = GetDC(A().hwndMain);
  const wchar_t* labels[4] = {L"All", L"Text", L"Images", L"Pinned"};
  int cx = s.rcChips.left;
  int hit = -1;
  for (int k = 0; k < 4; k++) {
    HFONT old = (HFONT)SelectObject(dc, u.hUi);
    SIZE sz;
    GetTextExtentPoint32W(dc, labels[k], (int)wcslen(labels[k]), &sz);
    SelectObject(dc, old);
    int cw = sz.cx + u.S(22);
    if (x >= cx && x < cx + cw) { hit = k; break; }
    cx += cw + u.S(6);
  }
  ReleaseDC(A().hwndMain, dc);
  return hit;
}

void InvalidateRow(HWND hwnd, PopupState& s, int idx) {
  if (idx < 0 || idx >= (int)s.view.size()) return;
  RECT rc = RowRect(s, idx);
  InvalidateRect(hwnd, &rc, FALSE);
}

void TrackHover(HWND hwnd) {
  PopupState& s = *S();
  if (!s.trackMouse) {
    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
    TrackMouseEvent(&tme);
    s.trackMouse = true;
  }
}

}  // namespace

// ---------------- edit subclass ----------------

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
  switch (m) {
    case WM_KEYDOWN:
      switch (w) {
        case VK_UP: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Up, 0); return 0;
        case VK_DOWN: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Down, 0); return 0;
        case VK_PRIOR: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::PgUp, 0); return 0;
        case VK_NEXT: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::PgDn, 0); return 0;
        case VK_RETURN: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Enter, 0); return 0;
        case VK_ESCAPE: PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Esc, 0); return 0;
        case VK_DELETE:
          PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Delete, 0);
          return 0;
        case 'P':
          if (GetKeyState(VK_CONTROL) & 0x8000) {
            PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Pin, 0);
            return 0;
          }
          break;
        case 'C':
          if (GetKeyState(VK_CONTROL) & 0x8000) {
            DWORD a = 0, b = 0;
            SendMessageW(h, EM_GETSEL, (WPARAM)&a, (LPARAM)&b);
            if (a == b) {  // no text selection -> copy the selected history item
              PostMessageW(A().hwndMain, WM_APP_KEY, (WPARAM)KeyCmd::Copy, 0);
              return 0;
            }
          }
          break;
        default:
          break;
      }
      break;
    case WM_MOUSEWHEEL: {
      // forward to the popup for scrolling
      HWND main = A().hwndMain;
      return SendMessageW(main, WM_MOUSEWHEEL, w, l);
    }
  }
  return DefSubclassProc(h, m, w, l);
}

// ---------------- popup window proc ----------------

static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  App& a = A();
  PopupState& s = *S();
  switch (msg) {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      Paint(hwnd);
      return 0;

    case WM_SIZE:
      Layout(hwnd);
      EnsureVisible(s);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;

    case WM_CTLCOLOREDIT: {
      HDC dc = (HDC)wParam;
      SetBkColor(dc, a.ui.th.surface);
      SetTextColor(dc, a.ui.th.text);
      static HBRUSH br = nullptr;
      static COLORREF madeFrom = 0xFFFFFFFF;
      if (!br || madeFrom != a.ui.th.surface) {  // theme switch: rebuild, don't reuse
        if (br) DeleteObject(br);
        br = CreateSolidBrush(a.ui.th.surface);
        madeFrom = a.ui.th.surface;
      }
      return (LRESULT)br;
    }

    case WM_ACTIVATE:
      if (LOWORD(wParam) == WA_INACTIVE && !g_openWithDialogOpen) {
        HidePreview();
        a.HidePopup(false);
      }
      return 0;

    case WM_TIMER:
      if (wParam == TIMER_PREVIEW) {
        PreviewTimerFired(hwnd);
        return 0;
      }
      return AppWndProc(hwnd, msg, wParam, lParam);  // app-owned timers

    case WM_COMMAND:
      // search-as-you-type: fired by the edit control on every keystroke only
      if (HIWORD(wParam) == EN_CHANGE && (HWND)lParam == a.hwndEdit) {
        wchar_t buf[256];
        int n = GetWindowTextW(a.hwndEdit, buf, 256);
        s.search.assign(buf, n);
        s.searchLower = ToLowerW(s.search);
        s.sel = 0;
        s.scroll = 0;
        RebuildView();
        PopupUpdateHeight(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_MOUSEMOVE: {
      TrackHover(hwnd);
      int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
      if (s.dragScroll) {
        int track = (s.rcList.bottom - s.rcList.top) - a.ui.S(8);
        int thumbH = track * (s.rcList.bottom - s.rcList.top) / (s.totalRowsPx ? s.totalRowsPx : 1);
        if (thumbH < a.ui.S(24)) thumbH = a.ui.S(24);
        if (track - thumbH > 0) {
          int delta = (y - s.dragY0) * MaxScroll(s) / (track - thumbH);
          int ns = s.dragScroll0 + delta;
          int m = MaxScroll(s);
          if (ns > m) ns = m;
          if (ns < 0) ns = 0;
          if (ns != s.scroll) {
            s.scroll = ns;
            InvalidateRect(hwnd, nullptr, FALSE);
          }
        }
        return 0;
      }
      int row = RowAtY(s, y);
      int newHover = row;
      int newBtn = 0;
      if (row >= 0) {
        RECT pr, dr;
        PinBtnRect(s, row, &pr);
        DelBtnRect(s, row, &dr);
        if (InRect(dr, x, y)) newBtn = 2;
        else if (InRect(pr, x, y)) newBtn = 1;
      }
      if (newHover != s.hover || newBtn != s.hoverBtn) {
        InvalidateRow(hwnd, s, s.hover);
        s.hover = newHover;
        s.hoverBtn = newBtn;
        InvalidateRow(hwnd, s, s.hover);
      }
      UpdatePreviewIntent(hwnd, newHover);
      return 0;
    }

    case WM_MOUSELEAVE:
      s.trackMouse = false;
      InvalidateRow(hwnd, s, s.hover);
      s.hover = -1;
      s.hoverBtn = 0;
      HidePreview();
      return 0;

    case WM_MOUSEWHEEL: {
      HidePreview();  // rows shift under the cursor
      int delta = GET_WHEEL_DELTA_WPARAM(wParam);
      int rows = (delta > 0 ? -3 : 3);  // one notch = 3 rows
      int ns = s.scroll + rows;
      int m = MaxScroll(s);
      if (ns > m) ns = m;
      if (ns < 0) ns = 0;
      if (ns != s.scroll) {
        s.scroll = ns;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONDOWN: {
      int x = GET_X_LPARAM(lParam), y = GET_Y_LPARAM(lParam);
      SetFocus(a.hwndEdit);
      // scrollbar?
      int listH = s.rcList.bottom - s.rcList.top;
      if (s.totalRowsPx > listH && x >= s.rcList.right - a.ui.S(12)) {
        HidePreview();
        s.dragScroll = true;
        s.dragY0 = y;
        s.dragScroll0 = s.scroll;
        SetCapture(hwnd);
        return 0;
      }
      int chip = ChipAt(s, x);
      if (chip >= 0 && InRect(s.rcChips, x, y)) {
        s.chip = chip;
        RebuildView();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
      }
      if (InRect(s.rcGear, x, y)) {
        OpenSettingsWindow();
        return 0;
      }
      int row = RowAtY(s, y);
      if (row >= 0) {
        RECT pr, dr;
        PinBtnRect(s, row, &pr);
        DelBtnRect(s, row, &dr);
        if (InRect(dr, x, y)) {
          a.history.Remove(s.view[row]->id);
          a.SchedulePersist();
          RebuildView();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        if (InRect(pr, x, y)) {
          a.history.SetPinned(s.view[row]->id, !s.view[row]->pinned);
          a.SchedulePersist();
          RebuildView();
          InvalidateRect(hwnd, nullptr, FALSE);
          return 0;
        }
        s.sel = row;
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;
    }

    case WM_LBUTTONDBLCLK: {
      int y = GET_Y_LPARAM(lParam);
      int row = RowAtY(s, y);
      if (row >= 0) {
        s.sel = row;
        a.RestoreAndPaste(s.view[row]);
      }
      return 0;
    }

    case WM_LBUTTONUP:
      if (s.dragScroll) {
        s.dragScroll = false;
        ReleaseCapture();
      }
      return 0;

    case WM_RBUTTONUP: {
      int y = GET_Y_LPARAM(lParam);
      int row = RowAtY(s, y);
      if (row < 0) return 0;
      s.sel = row;
      InvalidateRect(hwnd, nullptr, FALSE);
      POINT pt{GET_X_LPARAM(lParam), y};
      ClientToScreen(hwnd, &pt);
      HMENU m = CreatePopupMenu();
      AppendMenuW(m, MF_STRING, CM_ACTIVATE, L"Paste");
      AppendMenuW(m, MF_STRING, CM_COPY, L"Copy again");
      if (s.view[row]->type == ItemType::Image && !s.view[row]->imgFile.empty()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, CM_OPENPHOTO, L"Open in Photos");
        AppendMenuW(m, MF_STRING, CM_OPENWITH, L"Open with…");
        AppendMenuW(m, MF_STRING, CM_OPENLOC, L"Open file location");
      }
      AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
      AppendMenuW(m, MF_STRING | (s.view[row]->pinned ? MF_CHECKED : 0), CM_PIN,
                  s.view[row]->pinned ? L"Unpin" : L"Pin");
      AppendMenuW(m, MF_STRING, CM_DELETE, L"Delete");
      // stable id captured before the modal menu loop: a clipboard update can
      // mutate/reallocate history while the menu is open, invalidating pointers
      i64 menuItemId = s.view[row]->id;
      int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, pt.x, pt.y, 0,
                               hwnd, nullptr);
      DestroyMenu(m);
      Item* it = a.history.Find(menuItemId);  // re-resolve; nullptr if deleted meanwhile
      switch (cmd) {
        case CM_ACTIVATE:
          if (it) a.RestoreAndPaste(it);
          break;
        case CM_COPY:
          if (it) a.CopyAgain(*it);
          break;
        case CM_PIN:
          if (it) {
            a.history.SetPinned(it->id, !it->pinned);
            a.SchedulePersist();
            RebuildView();
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          break;
        case CM_DELETE:
          if (it) {
            a.history.Remove(it->id);
            a.SchedulePersist();
            RebuildView();
            InvalidateRect(hwnd, nullptr, FALSE);
          }
          break;
        case CM_OPENPHOTO:
          if (it && !it->imgFile.empty())
            ShellExecuteW(nullptr, L"open",
                          (BlobDir() + L"\\" + Utf8ToUtf16(it->imgFile)).c_str(), nullptr,
                          nullptr, SW_SHOWNORMAL);
          break;
        case CM_OPENWITH: {
          if (it && !it->imgFile.empty()) {
            // system "How do you want to open this file?" picker (shell32, Win7+)
            struct OPENASINFO_ { PCWSTR pcszFile; PCWSTR pcszClass; int oaifInFlags; };
            using PFN = HRESULT (WINAPI *)(HWND, const OPENASINFO_ *);
            HMODULE sh = GetModuleHandleW(L"shell32.dll");
            PFN openWith = sh ? (PFN)GetProcAddress(sh, "SHOpenWithDialog") : nullptr;
            wstring path = BlobDir() + L"\\" + Utf8ToUtf16(it->imgFile);
            g_openWithDialogOpen = true;  // keep the popup visible behind the picker
            bool launched = false;
            if (openWith) {
              OPENASINFO_ oi{};
              oi.pcszFile = path.c_str();
              oi.oaifInFlags = 0x1 | 0x4;  // allow "always use this app" + execute
              launched = SUCCEEDED(openWith(a.hwndMain, &oi));
            }
            if (!launched)  // very old shell: fall back to the openas verb
              ShellExecuteW(nullptr, L"openas", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            g_openWithDialogOpen = false;
          }
          break;
        }
        case CM_OPENLOC: {
          if (it && !it->imgFile.empty()) {
            wstring params = L"/select,\"" + BlobDir() + L"\\" + Utf8ToUtf16(it->imgFile) + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", params.c_str(), nullptr,
                          SW_SHOWNORMAL);
          }
          break;
        }
        default:
          break;
      }
      return 0;
    }

    case WM_DPICHANGED: {
      a.ui.ApplyDpi(hwnd, Settings::I().themeMode);
      RECT* r = (RECT*)lParam;
      SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                   SWP_NOZORDER | SWP_NOACTIVATE);
      Layout(hwnd);
      SendMessageW(a.hwndEdit, WM_SETFONT, (WPARAM)a.ui.hUi, TRUE);
      InvalidateRect(hwnd, nullptr, FALSE);
      return 0;
    }

    case WM_SETTINGCHANGE:
      if (lParam && CompareStringOrdinal((LPCWSTR)lParam, -1, L"ImmersiveColorSet", -1, TRUE) ==
                        CSTR_EQUAL) {
        a.ui.ApplyDpi(hwnd, Settings::I().themeMode);
        SendMessageW(a.hwndEdit, WM_SETFONT, (WPARAM)a.ui.hUi, TRUE);
        InvalidateRect(hwnd, nullptr, FALSE);
      }
      return 0;

    case WM_SETCURSOR:
      SetCursor(LoadCursor(nullptr, IDC_ARROW));
      return TRUE;

    default:
      // non-UI traffic (clipboard updates, hotkey, timers, tray, imaging) is
      // handled by the app-level proc
      return AppWndProc(hwnd, msg, wParam, lParam);
  }
}

// ---------------- public API ----------------

bool CreatePopupWindow(HINSTANCE hInst) {
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_DROPSHADOW;
  wc.lpfnWndProc = PopupWndProc;
  wc.hInstance = hInst;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  if (!RegisterClassExW(&wc)) return false;

  App& a = A();
  a.ui.ApplyDpi(nullptr, Settings::I().themeMode);

  a.hwndMain = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kClass, L"ClipVault",
                               WS_POPUP, 0, 0, a.ui.width, a.ui.height, nullptr, nullptr,
                               hInst, nullptr);
  if (!a.hwndMain) return false;

  // Win11 rounded corners (attribute 33, value 2 = ROUND); ignored by older systems
  HMODULE dwm = GetModuleHandleW(L"dwmapi.dll");
  if (dwm) {
    using Fn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    if (Fn f = (Fn)GetProcAddress(dwm, "DwmSetWindowAttribute")) {
      DWORD pref = 2;
      f(a.hwndMain, 33, &pref, sizeof(pref));
    }
  }

  a.hwndEdit = CreateWindowExW(0, L"EDIT", L"",
                               WS_CHILD | WS_VISIBLE | ES_LEFT | ES_AUTOHSCROLL,
                               0, 0, 100, 24, a.hwndMain, (HMENU)(INT_PTR)100, hInst, nullptr);
  SetWindowSubclass(a.hwndEdit, EditProc, 1, 0);
  SendMessageW(a.hwndEdit, WM_SETFONT, (WPARAM)a.ui.hUi, TRUE);
  SendMessageW(a.hwndEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"Search clipboard");

  Layout(a.hwndMain);
  RebuildView();
  return true;
}

void PopupHidePreview() {
  HidePreview();
}

void PopupReset() {
  PopupState& s = *S();
  App& a = A();
  s.chip = 0;
  s.sel = 0;
  s.scroll = 0;
  s.hover = -1;
  s.search.clear();
  s.searchLower.clear();
  SetWindowTextW(a.hwndEdit, L"");
  RebuildView();
}

void PopupRefresh() {
  RebuildView();
  if (IsWindowVisible(A().hwndMain)) {
    PopupUpdateHeight(A().hwndMain);
    InvalidateRect(A().hwndMain, nullptr, FALSE);
  }
}

void PopupRebuild() {
  Layout(A().hwndMain);
  RebuildView();
  InvalidateRect(A().hwndMain, nullptr, FALSE);
}

void PopupActivateSelection() {
  PopupState& s = *S();
  if (s.sel >= 0 && s.sel < (int)s.view.size()) A().RestoreAndPaste(s.view[s.sel]);
}

void PopupDeleteSelection() {
  PopupState& s = *S();
  if (s.sel >= 0 && s.sel < (int)s.view.size()) {
    A().history.Remove(s.view[s.sel]->id);
    A().SchedulePersist();
    RebuildView();
    InvalidateRect(A().hwndMain, nullptr, FALSE);
  }
}

void PopupPinSelection() {
  PopupState& s = *S();
  if (s.sel >= 0 && s.sel < (int)s.view.size()) {
    A().history.SetPinned(s.view[s.sel]->id, !s.view[s.sel]->pinned);
    A().SchedulePersist();
    RebuildView();
    InvalidateRect(A().hwndMain, nullptr, FALSE);
  }
}

void PopupCopySelection() {
  PopupState& s = *S();
  if (s.sel >= 0 && s.sel < (int)s.view.size()) A().CopyAgain(*s.view[s.sel]);
}

void PopupMoveSelection(KeyCmd cmd) {
  PopupState& s = *S();
  int step = cmd == KeyCmd::Up ? -1 : cmd == KeyCmd::Down ? 1
             : cmd == KeyCmd::PgUp       ? -s.visibleRows
                                         : s.visibleRows;
  s.sel += step;
  EnsureVisible(s);
  InvalidateRect(A().hwndMain, nullptr, FALSE);
}

}  // namespace cv
