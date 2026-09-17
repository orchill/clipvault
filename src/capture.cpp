#include "capture.h"

#include <shellapi.h>

namespace cv {

static constexpr size_t kMaxTextChars = 4000000;   // reject absurd paste payloads
static constexpr size_t kMaxBlobBytes = 8 << 20;   // RTF/HTML cap
static constexpr size_t kMaxImageBytes = 256 << 20;

static bool OpenClipboardRetry(HWND w) {
  // OLE clipboard owners (Office, browsers, .NET) hold the clipboard open
  // during SetClipboardData, and the update message reaches us while it is
  // still open. Wait up to ~1s, then give up gracefully (never blocks long).
  for (int i = 0; i < 50; i++) {
    if (OpenClipboard(w)) return true;
    Sleep(i < 10 ? 5 : 20);
  }
  return false;
}

static wstring GetSourceApp() {
  HWND owner = GetClipboardOwner();
  if (!owner) return L"";
  DWORD pid = 0;
  GetWindowThreadProcessId(owner, &pid);
  if (!pid) return L"";
  HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (!p) return L"";
  wchar_t buf[1024];
  DWORD sz = 1024;
  wstring out;
  if (QueryFullProcessImageNameW(p, 0, buf, &sz) && sz) out = BaseNameW(buf);
  CloseHandle(p);
  return out;
}

static bool CopyGlobal(HGLOBAL h, std::vector<u8>& out) {
  if (!h) return false;
  SIZE_T sz = GlobalSize(h);
  if (!sz || sz > kMaxImageBytes) return false;
  void* p = GlobalLock(h);
  if (!p) return false;
  out.assign((u8*)p, (u8*)p + sz);
  GlobalUnlock(h);
  return true;
}

static wstring CopyGlobalText(HGLOBAL h) {
  if (!h) return L"";
  SIZE_T sz = GlobalSize(h);
  const wchar_t* p = (const wchar_t*)GlobalLock(h);
  if (!p || sz < sizeof(wchar_t)) {
    if (p) GlobalUnlock(h);
    return L"";
  }
  SIZE_T chars = sz / sizeof(wchar_t);
  // clipboard text is not guaranteed NUL-terminated by all apps
  SIZE_T len = 0;
  while (len < chars && p[len]) len++;
  wstring out(p, len);
  GlobalUnlock(h);
  if (out.size() > kMaxTextChars) out.clear();
  return out;
}

static void ParsePngDims(const std::vector<u8>& png, int& w, int& h) {
  w = h = 0;
  // 8-byte signature | 4-byte len | "IHDR" | width(4 BE) height(4 BE)
  if (png.size() >= 24 && png[12] == 'I' && png[13] == 'H' && png[14] == 'D' && png[15] == 'R') {
    w = (png[16] << 24) | (png[17] << 16) | (png[18] << 8) | png[19];
    h = (png[20] << 24) | (png[21] << 16) | (png[22] << 8) | png[23];
  }
}

static size_t DibBitsOffset(const BITMAPINFOHEADER* h) {
  size_t off = h->biSize;
  if (h->biCompression == BI_BITFIELDS && h->biSize == sizeof(BITMAPINFOHEADER)) off += 12;
  if (h->biBitCount <= 8) {
    int colors = h->biClrUsed ? (int)h->biClrUsed : (1 << h->biBitCount);
    off += (size_t)colors * sizeof(RGBQUAD);
  }
  return off;
}

// Crude RTF -> text for preview/search when the source provides no plain text
// (some apps put only RTF on the clipboard).
static wstring RtfToText(const std::vector<u8>& rtf) {
  wstring out;
  bool skippingDest = false;  // {\fonttbl ...} / {\colortbl ...} etc.
  int groupDepth = 0;
  for (size_t k = 0; k < rtf.size(); k++) {
    char c = (char)rtf[k];
    if (c == '{') {
      groupDepth++;
      // known destination groups to skip entirely
      if (k + 12 <= rtf.size() && (memcmp(&rtf[k + 1], "\\fonttbl", 8) == 0 ||
                                 memcmp(&rtf[k + 1], "\\colortbl", 9) == 0 ||
                                 memcmp(&rtf[k + 1], "\\stylesheet", 11) == 0 ||
                                 memcmp(&rtf[k + 1], "\\info", 5) == 0 ||
                                 memcmp(&rtf[k + 1], "\\pict", 5) == 0))
        skippingDest = true;
      continue;
    }
    if (c == '}') {
      groupDepth--;
      if (groupDepth <= 0) { skippingDest = false; groupDepth = 0; }
      continue;
    }
    if (c == '\\') {
      size_t j = k + 1;
      if (j < rtf.size() && rtf[j] == '\'') {  // \'hh -> latin-1 char
        if (j + 2 < rtf.size()) {
          auto hex = [](char h) -> int {
            if (h >= '0' && h <= '9') return h - '0';
            if (h >= 'a' && h <= 'f') return h - 'a' + 10;
            if (h >= 'A' && h <= 'F') return h - 'A' + 10;
            return -1;
          };
          int hi = hex((char)rtf[j + 1]), lo = hex((char)rtf[j + 2]);
          if (hi >= 0 && lo >= 0) out += (wchar_t)((hi << 4) | lo);
        }
        k = j + 2;
        continue;
      }
      // control word: letters then optional number
      size_t wordStart = j;
      while (j < rtf.size() && ((rtf[j] >= 'a' && rtf[j] <= 'z') || (rtf[j] >= 'A' && rtf[j] <= 'Z'))) j++;
      size_t wordLen = j - wordStart;
      while (j < rtf.size() && (rtf[j] == '-' || (rtf[j] >= '0' && rtf[j] <= '9'))) j++;
      if (j < rtf.size() && rtf[j] == ' ') j++;
      if (wordLen == 3 && memcmp(&rtf[wordStart], "par", 3) == 0) out += L'\n';
      else if (wordLen == 3 && memcmp(&rtf[wordStart], "tab", 3) == 0) out += L'\t';
      k = j - 1;
      continue;
    }
    if (!skippingDest && (unsigned char)c >= 0x20 && c != '\n' && c != '\r') {
      if (out.size() < 8192) out += (wchar_t)(unsigned char)c;
    }
  }
  return out;
}

// Crude HTML -> text for preview/search.
static wstring HtmlToText(const std::vector<u8>& html) {
  wstring in = Utf8ToUtf16((const char*)html.data(), html.size());
  wstring out;
  bool inTag = false;
  for (wchar_t c : in) {
    if (!inTag && c == L'<') inTag = true;
    else if (inTag && c == L'>') inTag = false;
    else if (!inTag) out += c;
    if (out.size() > 8192) break;
  }
  return out;
}

CaptureResult CaptureClipboard() {
  CaptureResult r;
  UINT rtfFmt = RegisterClipboardFormatW(L"Rich Text Format");
  UINT htmlFmt = RegisterClipboardFormatW(L"HTML Format");
  UINT pngFmt = RegisterClipboardFormatW(L"PNG");

  if (!IsClipboardFormatAvailable(CF_UNICODETEXT) &&
      !IsClipboardFormatAvailable(rtfFmt) && !IsClipboardFormatAvailable(htmlFmt) &&
      !IsClipboardFormatAvailable(CF_DIB) && !IsClipboardFormatAvailable(CF_DIBV5) &&
      !IsClipboardFormatAvailable(pngFmt) && !IsClipboardFormatAvailable(CF_HDROP))
    return r;  // formats we don't understand -> skip silently

  if (!OpenClipboardRetry(nullptr)) return r;

  r.srcApp = GetSourceApp();

  // --- always grab the plain text if present (used by every type) ---
  bool hasText = IsClipboardFormatAvailable(CF_UNICODETEXT);
  if (hasText) {
    HANDLE h = GetClipboardData(CF_UNICODETEXT);
    if (h) r.text = CopyGlobalText((HGLOBAL)h);
  }

  auto bail = [&] {
    CloseClipboard();
    return CaptureResult{};
  };

  // --- priority: image > rtf > html > text > files ---
  UINT dibFmt = IsClipboardFormatAvailable(CF_DIBV5) ? CF_DIBV5
                : IsClipboardFormatAvailable(CF_DIB) ? CF_DIB : 0;
  if (dibFmt || IsClipboardFormatAvailable(pngFmt)) {
    r.type = ItemType::Image;
    if (IsClipboardFormatAvailable(pngFmt)) {
      if (!CopyGlobal((HGLOBAL)GetClipboardData(pngFmt), r.pngBytes)) return bail();
      ParsePngDims(r.pngBytes, r.imgW, r.imgH);
      if (r.imgW <= 0 || r.imgH <= 0) return bail();  // malformed PNG payload
      r.contentSize = r.pngBytes.size();
      if (!r.contentSize) return bail();
    } else {
      HGLOBAL h = (HGLOBAL)GetClipboardData(dibFmt);
      if (!h) return bail();
      SIZE_T sz = GlobalSize(h);
      const void* base = GlobalLock(h);
      if (!base || !sz || sz > kMaxImageBytes) {
        if (base) GlobalUnlock(h);
        return bail();
      }
      const BITMAPINFOHEADER* hdr = (const BITMAPINFOHEADER*)base;
      if (hdr->biWidth <= 0 || hdr->biHeight == 0 || (long long)hdr->biWidth * hdr->biHeight > 64'000'000) {
        GlobalUnlock(h);
        return bail();  // degenerate / >64MP: refuse (memory safety)
      }
      size_t off = DibBitsOffset(hdr);
      if (off >= sz) {
        GlobalUnlock(h);
        return bail();
      }
      r.dibHeader.assign((u8*)base, (u8*)base + off);
      r.dibBits.assign((u8*)base + off, (u8*)base + sz);
      r.imgW = hdr->biWidth;
      r.imgH = hdr->biHeight < 0 ? -hdr->biHeight : hdr->biHeight;
      if (hdr->biSize == sizeof(BITMAPV5HEADER)) {
        const BITMAPV5HEADER* v5 = (const BITMAPV5HEADER*)hdr;
        r.dibHasAlpha = v5->bV5AlphaMask != 0;
      } else {
        r.dibHasAlpha = hdr->biBitCount == 32;
      }
      GlobalUnlock(h);
      r.contentSize = sz;
    }
    r.ok = true;
    CloseClipboard();
    return r;
  }

  if (IsClipboardFormatAvailable(rtfFmt)) {
    if (!CopyGlobal((HGLOBAL)GetClipboardData(rtfFmt), r.rtfBytes)) return bail();
    if (r.rtfBytes.size() > kMaxBlobBytes) { CloseClipboard(); return CaptureResult{}; }
    r.type = ItemType::Rtf;
    r.contentSize = r.rtfBytes.size();
    if (r.text.empty()) r.text = RtfToText(r.rtfBytes);
    if (r.text.empty()) { CloseClipboard(); return CaptureResult{}; }
    r.ok = true;
    CloseClipboard();
    return r;
  }

  if (IsClipboardFormatAvailable(htmlFmt)) {
    if (!CopyGlobal((HGLOBAL)GetClipboardData(htmlFmt), r.htmlBytes)) return bail();
    if (r.htmlBytes.size() > kMaxBlobBytes) { CloseClipboard(); return CaptureResult{}; }
    r.type = ItemType::Html;
    r.contentSize = r.htmlBytes.size();
    if (r.text.empty()) r.text = HtmlToText(r.htmlBytes);
    if (r.text.empty()) { CloseClipboard(); return CaptureResult{}; }
    r.ok = true;
    CloseClipboard();
    return r;
  }

  if (!r.text.empty()) {
    r.type = ItemType::Text;
    r.contentSize = r.text.size() * 2;
    r.ok = true;
    CloseClipboard();
    return r;
  }

  if (IsClipboardFormatAvailable(CF_HDROP)) {
    HDROP drop = (HDROP)GetClipboardData(CF_HDROP);
    if (!drop) return bail();
    UINT n = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
    u64 totalChars = 0;
    for (UINT k = 0; k < n && k < 128; k++) {
      UINT len = DragQueryFileW(drop, k, nullptr, 0);
      if (!len) continue;
      std::vector<wchar_t> buf(len + 1);
      DragQueryFileW(drop, k, buf.data(), len + 1);
      wstring name(buf.data(), len);
      totalChars += name.size();
      if (totalChars > 200000) break;
      r.files.push_back(std::move(name));
    }
    // NOTE: the HDROP belongs to the clipboard — do not free it (no DragFinish).
    if (r.files.empty()) return bail();
    // preview text = newline-joined names (also the search text)
    for (auto& f : r.files) {
      r.text += f;
      r.text += L'\n';
    }
    r.type = ItemType::Files;
    r.contentSize = r.text.size() * 2;
    r.ok = true;
    CloseClipboard();
    return r;
  }

  CloseClipboard();
  return r;  // unknown combination
}

}  // namespace cv
