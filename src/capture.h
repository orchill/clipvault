// capture.h — extract clipboard formats (event-driven, runs only on WM_CLIPBOARDUPDATE).
#pragma once
#include "history.h"

namespace cv {

struct CaptureResult {
  bool ok = false;
  ItemType type = ItemType::Text;

  wstring text;  // plain/unicode text (always captured when present; used for preview+search)
  std::vector<u8> rtfBytes;
  std::vector<u8> htmlBytes;

  // image payload — exactly one of pngBytes / (dibHeader+dibBits)
  std::vector<u8> pngBytes;
  std::vector<u8> dibHeader;
  std::vector<u8> dibBits;
  int imgW = 0, imgH = 0;
  bool dibHasAlpha = false;

  std::vector<wstring> files;  // CF_HDROP names

  wstring srcApp;
  u64 contentSize = 0;  // payload size in bytes (display + image budget)
};

// Opens the clipboard (bounded retry), copies all relevant data out, closes it.
CaptureResult CaptureClipboard();

// Process name owning the clipboard at capture time is stored in CaptureResult::srcApp.

}  // namespace cv
