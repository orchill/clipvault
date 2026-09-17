#include "imaging.h"

#include <objidl.h>
#include <gdiplus.h>

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cv {

namespace {

HWND g_notify = nullptr;
UINT g_msg = 0;
std::thread g_worker;
std::mutex g_mx;
std::condition_variable g_cv;
std::deque<std::shared_ptr<ImageJob>> g_queue;
bool g_quit = false;

// ---- DIB -> BGRA -----------------------------------------------------------

// Scales any bitmap into a square thumbnail (letterboxed); caller owns result.
void* MakeThumbFromBitmap(Gdiplus::Bitmap* src, int boxPx) {
  if (!src || src->GetLastStatus() != Gdiplus::Ok) return nullptr;
  int w = (int)src->GetWidth(), h = (int)src->GetHeight();
  if (!w || !h) return nullptr;
  Gdiplus::Bitmap* t = new Gdiplus::Bitmap(boxPx, boxPx, PixelFormat32bppARGB);
  Gdiplus::Graphics g(t);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  Gdiplus::Rect dst(0, 0, boxPx, boxPx);
  if (w > h) {
    int tw = boxPx * w / h;
    dst = Gdiplus::Rect((boxPx - tw) / 2, 0, tw, boxPx);
  } else if (h > w) {
    int th = boxPx * h / w;
    dst = Gdiplus::Rect(0, (boxPx - th) / 2, boxPx, th);
  }
  g.DrawImage(src, dst, 0, 0, w, h, Gdiplus::UnitPixel);
  return t;
}

// Fills alpha with 0xFF when every alpha byte is zero. Some apps (e.g. Office)
// put uninitialized/all-zero alpha on the clipboard while the image is opaque.
void FixAlpha(std::vector<u8>& bgra, bool hasAlphaHint) {
  if (!hasAlphaHint) {
    for (size_t k = 3; k < bgra.size(); k += 4) bgra[k] = 0xFF;
    return;
  }
  bool anyAlpha = false;
  for (size_t k = 3; k < bgra.size(); k += 4) {
    if (bgra[k]) { anyAlpha = true; break; }
  }
  if (!anyAlpha) {
    for (size_t k = 3; k < bgra.size(); k += 4) bgra[k] = 0xFF;
  }
}

void ProcessJob(std::shared_ptr<ImageJob> job) {
  Item& it = job->item;
  std::vector<u8> bgra;
  int w = 0, h = 0;
  wstring base = BlobDir() + L"\\" + Utf8ToUtf16(HashToHex(it.hash));

  if (job->thumbOnly) {
    // restart path: rebuild only the thumbnail from the stored blob
    wstring path = BlobDir() + L"\\" + Utf8ToUtf16(it.imgFile);
    Gdiplus::Bitmap decoded(path.c_str());
    if (decoded.GetLastStatus() == Gdiplus::Ok)
      it.thumb = MakeThumbFromBitmap(&decoded, 96);
    ImageResult* res = new ImageResult();
    res->thumbOnly = true;
    res->item = std::move(job->item);
    PostMessageW(g_notify, g_msg, 0, (LPARAM)res);
    return;
  }

  if (!job->pngBytes.empty()) {
    // "PNG" clipboard format: bytes are already encoded — write them directly.
    it.hash = Fnv64(job->pngBytes.data(), job->pngBytes.size());
    base = BlobDir() + L"\\" + Utf8ToUtf16(HashToHex(it.hash)) + L".png";
    if (!WriteAllBytesAtomic(base, job->pngBytes.data(), job->pngBytes.size())) return;
    it.imgFile = HashToHex(it.hash) + ".png";
    it.imgJpeg = false;
    it.size = job->pngBytes.size();

    Gdiplus::Bitmap decoded(base.c_str());
    if (decoded.GetLastStatus() == Gdiplus::Ok) {
      w = (int)decoded.GetWidth();
      h = (int)decoded.GetHeight();
      it.imgW = w;
      it.imgH = h;
      it.thumb = MakeThumbFromBitmap(&decoded, 96);
    }
  } else {
    if (!DibToBgra(job->dibHeader, job->dibBits, job->dibHasAlpha, bgra, w, h)) return;
    it.imgW = w;
    it.imgH = h;
    // stable content hash (independent of chosen container format)
    it.hash = Fnv64Combine(Fnv64Combine(Fnv64(bgra.data(), bgra.size()), (u64)w), (u64)h);
    base = BlobDir() + L"\\" + Utf8ToUtf16(HashToHex(it.hash));

    Gdiplus::Bitmap wrapped(w, h, w * 4, PixelFormat32bppARGB, bgra.data());
    CLSID pngClsid, jpgClsid;
    bool havePng = GetEncoderClsid(L"image/png", &pngClsid);
    bool haveJpg = GetEncoderClsid(L"image/jpeg", &jpgClsid);

    bool saved = false;
    if (havePng) {
      Gdiplus::Status st = wrapped.Save((base + L".png").c_str(), &pngClsid, nullptr);
      if (st == Gdiplus::Ok) {
        u64 sz = FileSizeW(base + L".png");
        // Disk economy: huge opaque screenshots compress far better as JPEG.
        if (haveJpg && sz > (2 << 20) && !job->dibHasAlpha) {
          Gdiplus::EncoderParameters ep;
          ep.Count = 1;
          ep.Parameter[0].Guid = Gdiplus::EncoderQuality;
          ep.Parameter[0].Type = Gdiplus::EncoderParameterValueTypeLong;
          ep.Parameter[0].NumberOfValues = 1;
          ULONG q = 85;
          ep.Parameter[0].Value = &q;
          if (wrapped.Save((base + L".jpg").c_str(), &jpgClsid, &ep) == Gdiplus::Ok) {
            DeleteFileW((base + L".png").c_str());
            it.imgFile = HashToHex(it.hash) + ".jpg";
            it.imgJpeg = true;
            it.size = FileSizeW(base + L".jpg");
            saved = true;
          }
        }
        if (!saved) {
          it.imgFile = HashToHex(it.hash) + ".png";
          it.imgJpeg = false;
          it.size = sz;
          saved = true;
        }
      }
    }
    if (!saved) return;

    it.thumb = MakeThumbFromBitmap(&wrapped, 96);
  }

  ImageResult* res = new ImageResult();
  res->item = std::move(job->item);
  PostMessageW(g_notify, g_msg, 0, (LPARAM)res);
}

void WorkerLoop() {
  for (;;) {
    std::shared_ptr<ImageJob> job;
    {
      std::unique_lock<std::mutex> lk(g_mx);
      g_cv.wait(lk, [] { return g_quit || !g_queue.empty(); });
      if (g_quit && g_queue.empty()) return;
      job = g_queue.front();
      g_queue.pop_front();
    }
    ProcessJob(job);
  }
}

}  // namespace

void ImagingStart(HWND notifyWnd, UINT msg) {
  g_notify = notifyWnd;
  g_msg = msg;
  std::lock_guard<std::mutex> lk(g_mx);
  g_quit = false;
  if (!g_worker.joinable()) g_worker = std::thread(WorkerLoop);
}

void ImagingStop() {
  {
    std::lock_guard<std::mutex> lk(g_mx);
    g_quit = true;
    std::deque<std::shared_ptr<ImageJob>>().swap(g_queue);
  }
  g_cv.notify_all();
  if (g_worker.joinable()) g_worker.join();
}

void ImagingPost(std::shared_ptr<ImageJob> job) {
  {
    std::lock_guard<std::mutex> lk(g_mx);
    if (g_queue.size() > 4) g_queue.pop_front();  // rapid-fire copies: keep the freshest
    g_queue.push_back(std::move(job));
  }
  g_cv.notify_one();
}

// ---- shared helpers --------------------------------------------------------

bool DibToBgra(const std::vector<u8>& header, const std::vector<u8>& bits, bool alphaHint,
               std::vector<u8>& bgra, int& w, int& h) {
  w = h = 0;
  if (header.size() < sizeof(BITMAPINFOHEADER)) return false;
  const BITMAPINFOHEADER* bi = (const BITMAPINFOHEADER*)header.data();
  w = bi->biWidth;
  h = bi->biHeight < 0 ? -bi->biHeight : bi->biHeight;
  if (w <= 0 || h <= 0 || (long long)w * h > 64'000'000) return false;

  size_t stride = ((size_t)w * bi->biBitCount + 31) / 32 * 4;

  if (bi->biBitCount == 32) {
    bgra.resize((size_t)w * 4 * h);
    bool topDown = bi->biHeight < 0;
    for (int row = 0; row < h; row++) {
      const u8* src = bits.data() + (size_t)row * stride;
      u8* dst = bgra.data() + (size_t)(topDown ? row : h - 1 - row) * w * 4;
      memcpy(dst, src, (size_t)w * 4);
    }
    FixAlpha(bgra, alphaHint);
    return true;
  }

  if (bi->biBitCount == 24) {
    bgra.resize((size_t)w * 4 * h);
    bool topDown = bi->biHeight < 0;
    for (int row = 0; row < h; row++) {
      const u8* src = bits.data() + (size_t)row * stride;
      u8* dst = bgra.data() + (size_t)(topDown ? row : h - 1 - row) * w * 4;
      for (int x = 0; x < w; x++) {
        dst[x * 4 + 0] = src[x * 3 + 0];
        dst[x * 4 + 1] = src[x * 3 + 1];
        dst[x * 4 + 2] = src[x * 3 + 2];
        dst[x * 4 + 3] = 0xFF;
      }
    }
    return true;
  }

  // exotic depth (1/4/8/16 bpp): normalize through GDI
  BITMAPINFOHEADER src = *bi;
  src.biHeight = -h;  // force top-down target semantics below via BitBlt
  std::vector<u8> full = header;
  full.insert(full.end(), bits.begin(), bits.end());
  void* sectionBits = nullptr;
  HBITMAP hsrc = CreateDIBSection(nullptr, (const BITMAPINFO*)full.data(), DIB_RGB_COLORS,
                                  &sectionBits, nullptr, 0);
  if (!hsrc || !sectionBits) return false;
  // copy the source rows bottom-up as stored
  {
    size_t copyRows = h;
    for (int row = 0; row < (int)copyRows; row++) {
      memcpy((u8*)sectionBits + (size_t)row * stride, bits.data() + (size_t)row * stride, stride);
    }
  }

  BITMAPINFOHEADER dst = *bi;
  dst.biSize = sizeof(BITMAPINFOHEADER);
  dst.biBitCount = 32;
  dst.biCompression = BI_RGB;
  dst.biHeight = -h;
  dst.biSizeImage = 0;
  BITMAPINFO dstInfo{};
  dstInfo.bmiHeader = dst;
  void* dstBits = nullptr;
  HBITMAP hdst = CreateDIBSection(nullptr, &dstInfo, DIB_RGB_COLORS, &dstBits, nullptr, 0);
  bool ok = false;
  if (hdst && dstBits) {
    HDC sdc = CreateCompatibleDC(nullptr);
    HDC ddc = CreateCompatibleDC(nullptr);
    HGDIOBJ os = SelectObject(sdc, hsrc);
    HGDIOBJ od = SelectObject(ddc, hdst);
    ok = BitBlt(ddc, 0, 0, w, h, sdc, 0, 0, SRCCOPY) != 0;
    SelectObject(sdc, os);
    SelectObject(ddc, od);
    DeleteDC(sdc);
    DeleteDC(ddc);
    if (ok) {
      bgra.assign((u8*)dstBits, (u8*)dstBits + (size_t)w * 4 * h);
      FixAlpha(bgra, false);
    }
  }
  if (hdst) DeleteObject(hdst);
  if (hsrc) DeleteObject(hsrc);
  return ok;
}

void* MakeThumb(const std::vector<u8>& bgra, int w, int h, int boxPx, int /*dpi*/) {
  if (bgra.empty() || w <= 0 || h <= 0) return nullptr;
  Gdiplus::Bitmap wrapped(w, h, w * 4, PixelFormat32bppARGB, (BYTE*)bgra.data());
  Gdiplus::Bitmap* t = new Gdiplus::Bitmap(boxPx, boxPx, PixelFormat32bppARGB);
  Gdiplus::Graphics g(t);
  g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
  Gdiplus::Rect dst(0, 0, boxPx, boxPx);
  if (w > h) {
    int tw = boxPx * w / h;
    dst = Gdiplus::Rect((boxPx - tw) / 2, 0, tw, boxPx);
  } else if (h > w) {
    int th = boxPx * h / w;
    dst = Gdiplus::Rect(0, (boxPx - th) / 2, boxPx, th);
  }
  g.DrawImage(&wrapped, dst, 0, 0, w, h, Gdiplus::UnitPixel);
  return t;
}

bool GetEncoderClsid(const wchar_t* mime, CLSID* out) {
  UINT n = 0, sz = 0;
  Gdiplus::GetImageEncodersSize(&n, &sz);
  if (!n || !sz) return false;
  std::vector<u8> buf(sz);
  Gdiplus::ImageCodecInfo* info = (Gdiplus::ImageCodecInfo*)buf.data();
  if (Gdiplus::GetImageEncoders(n, sz, info) != Gdiplus::Ok) return false;
  for (UINT k = 0; k < n; k++) {
    if (wcscmp(info[k].MimeType, mime) == 0) {
      *out = info[k].Clsid;
      return true;
    }
  }
  return false;
}

}  // namespace cv
