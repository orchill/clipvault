// theme.h — palette, fonts and DPI-scaled metrics for the custom-painted UI.
#pragma once
#include "util.h"

namespace cv {

struct Theme {
  bool dark = true;
  COLORREF bg = 0;         // window background
  COLORREF surface = 0;    // search box / cards
  COLORREF hover = 0;      // hovered card
  COLORREF sel = 0;        // selected card fill
  COLORREF stroke = 0;     // selected card border
  COLORREF text = 0;       // primary text
  COLORREF textDim = 0;    // secondary text
  COLORREF accent = 0;     // selection/pin accent
  COLORREF chipActive = 0; // active filter pill bg
  COLORREF chipIdle = 0;   // idle pill bg
  COLORREF scrollbar = 0;  // thumb
  COLORREF sep = 0;        // separator line
  COLORREF placeholder = 0;
};

struct UiCtx {
  int dpi = 96;
  Theme th;
  HFONT hUi = nullptr;       // body text
  HFONT hUiBold = nullptr;
  HFONT hUiSmall = nullptr;  // meta line
  HFONT hIcon = nullptr;     // Segoe Fluent Icons / MDL2 glyphs
  int rowH = 0;              // px
  int rowHImage = 0;
  int pad = 0;               // outer padding
  int searchH = 0;
  int chipsH = 0;
  int footerH = 0;
  int thumb = 0;             // thumbnail box px
  int width = 0;             // popup default width
  int height = 0;

  void ApplyDpi(HWND hwnd, int themeMode);
  int S(int dip) const { return MulDiv(dip, dpi, 96); }
};

// icon glyph codepoints (Segoe Fluent Icons / Segoe MDL2 Assets)
enum IconGlyph {
  GLYPH_SEARCH = 0xE721,
  GLYPH_PIN = 0xE718,
  GLYPH_UNPIN = 0xE77A,
  GLYPH_DELETE = 0xE74D,
  GLYPH_GEAR = 0xE713,
  GLYPH_DOC = 0xE8A5,
  GLYPH_PICTURE = 0xE8B9,
  GLYPH_CODE = 0xE943,
  GLYPH_FOLDER = 0xE8B7,
  GLYPH_COPY = 0xE8C8,
  GLYPH_EMOJI = 0xE76E,
  GLYPH_HISTORY = 0xE81C,
};

COLORREF Mix(COLORREF a, COLORREF b, int pctB);  // blend for pseudo-alpha on GDI fills

}  // namespace cv
