// imaging.h — background image processing (encode to disk + thumbnail generation).
#pragma once
#include "history.h"

namespace cv {

// One queued image job. The UI thread fills `item` + raw payload; the worker
// encodes/commits `imgFile`, computes `hash`, and hands a finished Item back.
struct ImageJob {
  Item item;                 // partially filled (ts, srcApp, imgW/H); worker completes it
  std::vector<u8> pngBytes;  // PNG clipboard format (takes precedence)
  std::vector<u8> dibHeader;
  std::vector<u8> dibBits;
  bool dibHasAlpha = false;
  bool thumbOnly = false;    // regenerate a thumbnail from item.imgFile (restart path)
};

// Result posted to the UI thread as WM_APP+lParam (heap pointer; UI frees).
struct ImageResult {
  Item item;      // complete; imgFile/hash/size filled; thumb set (may be null)
  bool thumbOnly = false;
};

using NotifyFn = void (*)(ImageResult*);  // posted via app-provided bridge

void ImagingStart(HWND notifyWnd, UINT msg);
void ImagingStop();
void ImagingPost(std::shared_ptr<ImageJob> job);

// Converts a raw clipboard DIB to 32bpp top-down BGRA (alpha fixed to opaque when absent).
bool DibToBgra(const std::vector<u8>& header, const std::vector<u8>& bits, bool alphaHint,
               std::vector<u8>& bgra, int& w, int& h);

// Generates a 96px thumbnail bitmap (caller owns the returned Bitmap*).
void* MakeThumb(const std::vector<u8>& bgra, int w, int h, int boxPx, int dpi);

// Encoder CLSID lookup (PNG/JPEG).
bool GetEncoderClsid(const wchar_t* mime, CLSID* out);

}  // namespace cv
