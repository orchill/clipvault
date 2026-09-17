#include "restore.h"

#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>

namespace cv {

namespace {

class ClipboardWriter {
 public:
  explicit ClipboardWriter(HWND owner) : ok_(OpenClipboard(owner) != 0) {
    if (ok_) {
      if (!EmptyClipboard()) { CloseClipboard(); ok_ = false; }
    }
  }
  ~ClipboardWriter() {
    if (ok_) CloseClipboard();
    for (HGLOBAL h : leftover_) GlobalFree(h);  // only failed allocations land here
  }
  bool ok() const { return ok_; }

  // Allocates a moveable HGLOBAL, copies bytes, transfers ownership on success.
  bool Set(UINT fmt, const void* data, size_t bytes) {
    if (!ok_) return false;
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes ? bytes : 1);
    if (!h) return false;
    void* p = GlobalLock(h);
    if (!p) { GlobalFree(h); return false; }
    if (bytes) memcpy(p, data, bytes);
    GlobalUnlock(h);
    if (SetClipboardData(fmt, h)) return true;  // system owns it now
    leftover_.push_back(h);
    return false;
  }

  bool SetText(const wstring& s) {
    return Set(CF_UNICODETEXT, s.c_str(), (s.size() + 1) * sizeof(wchar_t));
  }

 private:
  bool ok_;
  std::vector<HGLOBAL> leftover_;
};

void AppendDibFromBitmap(Gdiplus::Bitmap* bmp, bool withAlpha, std::vector<u8>& out) {
  int w = bmp->GetWidth(), h = bmp->GetHeight();
  Gdiplus::BitmapData bd;
  Gdiplus::Rect rc(0, 0, w, h);
  if (bmp->LockBits(&rc, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &bd) != Gdiplus::Ok)
    return;
  const u8* src = (const u8*)bd.Scan0;

  if (withAlpha) {
    BITMAPV5HEADER v5{};
    v5.bV5Size = sizeof(BITMAPV5HEADER);
    v5.bV5Width = w;
    v5.bV5Height = h;  // positive -> bottom-up rows
    v5.bV5Planes = 1;
    v5.bV5BitCount = 32;
    v5.bV5Compression = BI_BITFIELDS;
    v5.bV5SizeImage = (DWORD)w * 4 * h;
    v5.bV5RedMask = 0x00FF0000;
    v5.bV5GreenMask = 0x0000FF00;
    v5.bV5BlueMask = 0x000000FF;
    v5.bV5AlphaMask = 0xFF000000;
    size_t head = sizeof(v5);
    out.resize(head + (size_t)w * 4 * h);
    memcpy(out.data(), &v5, head);
    for (int row = 0; row < h; row++) {  // flip to bottom-up
      memcpy(out.data() + head + (size_t)(h - 1 - row) * w * 4, src + (size_t)row * bd.Stride,
             (size_t)w * 4);
    }
  } else {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = w;
    bi.biHeight = h;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_BITFIELDS;
    bi.biSizeImage = (DWORD)w * 4 * h;
    size_t head = sizeof(bi) + 3 * sizeof(DWORD);  // 40-byte header + 3 masks
    out.resize(head + (size_t)w * 4 * h);
    memcpy(out.data(), &bi, sizeof(bi));
    DWORD* masks = (DWORD*)(out.data() + sizeof(bi));
    masks[0] = 0x00FF0000;
    masks[1] = 0x0000FF00;
    masks[2] = 0x000000FF;
    for (int row = 0; row < h; row++) {
      memcpy(out.data() + head + (size_t)(h - 1 - row) * w * 4, src + (size_t)row * bd.Stride,
             (size_t)w * 4);
    }
  }
  bmp->UnlockBits(&bd);
}

bool RestoreImage(const Item& it, ClipboardWriter& w) {
  wstring path = BlobDir() + L"\\" + Utf8ToUtf16(it.imgFile);
  std::vector<u8> bytes;
  if (!ReadAllBytes(path, bytes) || bytes.empty()) return false;

  UINT pngFmt = RegisterClipboardFormatW(L"PNG");
  if (!it.imgJpeg) w.Set(pngFmt, bytes.data(), bytes.size());  // opportunistic

  Gdiplus::Bitmap bmp(path.c_str());

  std::vector<u8> dibV5, dib;
  if (bmp.GetLastStatus() == Gdiplus::Ok) {
    AppendDibFromBitmap(&bmp, true, dibV5);
    AppendDibFromBitmap(&bmp, false, dib);
  }
  if (!dibV5.empty()) w.Set(CF_DIBV5, dibV5.data(), dibV5.size());
  if (!dib.empty()) w.Set(CF_DIB, dib.data(), dib.size());
  return !dibV5.empty() || !dib.empty();
}

bool RestoreFiles(const Item& it, ClipboardWriter& w) {
  std::vector<wchar_t> names;
  size_t pos = 0;
  const wstring& t = it.text;
  while (pos < t.size()) {
    size_t nl = t.find(L'\n', pos);
    if (nl == wstring::npos) nl = t.size();
    wstring name = t.substr(pos, nl - pos);
    pos = nl + 1;
    if (name.empty()) continue;
    names.insert(names.end(), name.begin(), name.end());
    names.push_back(L'\0');
  }
  if (names.empty()) return false;
  names.push_back(L'\0');  // double NUL terminator

  size_t bytes = sizeof(DROPFILES) + names.size() * sizeof(wchar_t);
  std::vector<u8> buf(bytes, 0);
  DROPFILES* df = (DROPFILES*)buf.data();
  df->pFiles = sizeof(DROPFILES);
  df->fWide = TRUE;
  memcpy(buf.data() + sizeof(DROPFILES), names.data(), names.size() * sizeof(wchar_t));
  return w.Set(CF_HDROP, buf.data(), buf.size());
}

wstring LoadFullText(const Item& it) {
  if (it.blobFile.empty() || it.blobFile.size() < 4 ||
      it.blobFile.compare(it.blobFile.size() - 4, 4, ".txt") != 0)
    return it.text;  // fully resident
  std::vector<u8> raw;
  if (!ReadAllBytes(BlobDir() + L"\\" + Utf8ToUtf16(it.blobFile), raw)) return it.text;
  wstring full = Utf8ToUtf16((const char*)raw.data(), raw.size());
  return full.empty() ? it.text : full;
}

}  // namespace

bool RestoreItemToClipboard(const Item& it) {
  ClipboardWriter w(nullptr);  // attach to current task's clipboard chain
  if (!w.ok()) return false;

  switch (it.type) {
    case ItemType::Image:
      return RestoreImage(it, w) || w.SetText(it.text);
    case ItemType::Rtf: {
      bool any = false;
      if (!it.blobFile.empty()) {
        std::vector<u8> raw;
        if (ReadAllBytes(BlobDir() + L"\\" + Utf8ToUtf16(it.blobFile), raw) && !raw.empty()) {
          UINT rtfFmt = RegisterClipboardFormatW(L"Rich Text Format");
          any = w.Set(rtfFmt, raw.data(), raw.size());
        }
      }
      return w.SetText(LoadFullText(it)) || any;
    }
    case ItemType::Html: {
      bool any = false;
      if (!it.blobFile.empty()) {
        std::vector<u8> raw;
        if (ReadAllBytes(BlobDir() + L"\\" + Utf8ToUtf16(it.blobFile), raw) && !raw.empty()) {
          UINT htmlFmt = RegisterClipboardFormatW(L"HTML Format");
          any = w.Set(htmlFmt, raw.data(), raw.size());
        }
      }
      return w.SetText(LoadFullText(it)) || any;
    }
    case ItemType::Files:
      return RestoreFiles(it, w);
    case ItemType::Text:
    default:
      return w.SetText(LoadFullText(it));
  }
}

}  // namespace cv
