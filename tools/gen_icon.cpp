// gen_icon.cpp — generates resources/app.ico (pure C++, no dependencies).
// A modern flat clipboard glyph: accent rounded square, white paper, clip, lines.
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <vector>

struct Img {
  int n;
  std::vector<uint8_t> px;  // BGRA, top-down
  Img(int size) : n(size), px((size_t)size * size * 4, 0) {}
  void set(int x, int y, uint8_t b, uint8_t g, uint8_t r, uint8_t a) {
    if (x < 0 || y < 0 || x >= n || y >= n) return;
    size_t i = ((size_t)y * n + x) * 4;
    px[i] = b; px[i + 1] = g; px[i + 2] = r; px[i + 3] = a;
  }
  void fill(int x0, int y0, int x1, int y1, uint8_t b, uint8_t g, uint8_t r, uint8_t a = 255) {
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) set(x, y, b, g, r, a);
  }
};

static bool InRoundRect(double x, double y, double x0, double y0, double x1, double y1, double r) {
  if (x < x0 || y < y0 || x > x1 || y > y1) return false;
  double cx = x < x0 + r ? x0 + r : (x > x1 - r ? x1 - r : x);
  double cy = y < y0 + r ? y0 + r : (y > y1 - r ? y1 - r : y);
  double dx = x - cx, dy = y - cy;
  return dx * dx + dy * dy <= r * r || (x >= x0 + r && x <= x1 - r) || (y >= y0 + r && y <= y1 - r);
}

// anti-aliased coverage for a rounded rect edge (supersample 2x2)
static double RoundRectCoverage(double x, double y, double x0, double y0, double x1, double y1,
                                double r) {
  int hit = 0;
  for (int sy = 0; sy < 2; sy++)
    for (int sx = 0; sx < 2; sx++)
      if (InRoundRect(x + 0.25 + 0.5 * sx, y + 0.25 + 0.5 * sy, x0, y0, x1, y1, r)) hit++;
  return hit / 4.0;
}

static void Blend(Img& im, int x, int y, uint8_t b, uint8_t g, uint8_t r, double cov) {
  if (cov <= 0) return;
  if (x < 0 || y < 0 || x >= im.n || y >= im.n) return;
  size_t i = ((size_t)y * im.n + x) * 4;
  double a = cov * 255.0;
  double dstA = im.px[i + 3];
  double outA = a + dstA * (1.0 - a / 255.0);
  if (outA <= 0) return;
  for (int k = 0; k < 3; k++) {
    double src = (double)((const uint8_t*)&b)[k];
    double dst = im.px[i + k];
    im.px[i + k] = (uint8_t)((src * a + dst * dstA * (1.0 - a / 255.0)) / outA + 0.5);
  }
  im.px[i + 3] = (uint8_t)(outA + 0.5);
}

static void DrawRoundRect(Img& im, double x0, double y0, double x1, double y1, double r,
                          uint8_t b, uint8_t g, uint8_t rr, uint8_t a = 255) {
  for (int y = (int)y0 - 1; y <= (int)y1 + 1; y++)
    for (int x = (int)x0 - 1; x <= (int)x1 + 1; x++) {
      double cov = RoundRectCoverage(x, y, x0, y0, x1, y1, r);
      if (cov > 0) Blend(im, x, y, b, g, rr, cov * a / 255.0);
    }
}

int main(int argc, char** argv) {
  const char* out = argc > 1 ? argv[1] : "resources/app.ico";

  struct Entry {
    int n;
    std::vector<uint8_t> bmp;  // BITMAPINFOHEADER + XOR + AND
  };
  std::vector<Entry> entries;

  for (int n : {16, 24, 32, 48, 64, 128, 256}) {
    Img im(n);
    double m = n * 0.06;                 // margin
    double x0 = m, y0 = m, x1 = n - m, y1 = n - m;
    double rad = n * 0.22;

    // board: vertical gradient #4FC3FF -> #2470E8
    for (int y = 0; y < n; y++) {
      double t = (double)y / n;
      uint8_t b = (uint8_t)(0xFF - t * (0xFF - 0xE8));
      uint8_t g = (uint8_t)(0xC3 - t * (0xC3 - 0x70));
      uint8_t r = (uint8_t)(0x4F - t * (0x4F - 0x24));
      for (int x = 0; x < n; x++) {
        double cov = RoundRectCoverage(x, y, x0, y0, x1, y1, rad);
        if (cov > 0) Blend(im, x, y, b, g, r, cov);
      }
    }

    // paper (white card, slightly rotated look avoided for simplicity)
    double px0 = n * 0.24, py0 = n * 0.22, px1 = n * 0.76, py1 = n * 0.88;
    DrawRoundRect(im, px0, py0, px1, py1, n * 0.06, 0xFF, 0xFF, 0xFF);

    // clip (dark accent tab on top of paper)
    double cx0 = n * 0.40, cy0 = n * 0.16, cx1 = n * 0.60, cy1 = n * 0.30;
    DrawRoundRect(im, cx0, cy0, cx1, cy1, n * 0.05, 0xB0, 0x3A, 0x0E);  // #0E3AB0 BGR

    // text lines on the paper: two gray + one accent
    double lx0 = n * 0.31, lx1 = n * 0.69;
    DrawRoundRect(im, lx0, n * 0.40, lx1, n * 0.44, n * 0.02, 0xD6, 0xCE, 0xC9);  // #C9CED6
    DrawRoundRect(im, lx0, n * 0.52, lx1, n * 0.56, n * 0.02, 0xD6, 0xCE, 0xC9);
    DrawRoundRect(im, lx0, n * 0.64, n * 0.55, n * 0.68, n * 0.02, 0xF0, 0x8F, 0x35);  // accent

    // pack as BMP-in-ICO (bottom-up rows + AND mask)
    size_t rowBytes = ((size_t)n + 31) / 32 * 4;
    size_t maskSize = rowBytes * n;
    Entry e;
    e.n = n;
    e.bmp.resize(40 + (size_t)n * n * 4 + maskSize, 0);
    BITMAPINFOHEADER bi{};
    bi.biSize = 40;
    bi.biWidth = n;
    bi.biHeight = n * 2;  // XOR + AND
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biSizeImage = (DWORD)((size_t)n * n * 4 + maskSize);
    memcpy(e.bmp.data(), &bi, 40);
    for (int y = 0; y < n; y++) {
      memcpy(e.bmp.data() + 40 + (size_t)(n - 1 - y) * n * 4, &im.px[(size_t)y * n * 4],
             (size_t)n * 4);
      // AND mask stays zero: alpha channel is authoritative
    }
    entries.push_back(std::move(e));
  }

  // assemble the ICO file
  std::vector<uint8_t> ico;
  auto put16 = [&ico](uint16_t v) {
    ico.push_back(v & 0xFF);
    ico.push_back((v >> 8) & 0xFF);
  };
  auto put32 = [&ico](uint32_t v) {
    ico.push_back(v & 0xFF);
    ico.push_back((v >> 8) & 0xFF);
    ico.push_back((v >> 16) & 0xFF);
    ico.push_back((v >> 24) & 0xFF);
  };
  put16(0);
  put16(1);
  put16((uint16_t)entries.size());
  size_t offset = 6 + entries.size() * 16;
  for (auto& e : entries) {
    ico.push_back(e.n >= 256 ? 0 : e.n);  // width
    ico.push_back(e.n >= 256 ? 0 : e.n);  // height
    ico.push_back(0);                     // colors
    ico.push_back(0);                     // reserved
    put16(1);                             // planes
    put16(32);                            // bpp
    put32((uint32_t)e.bmp.size());
    put32((uint32_t)offset);
    offset += e.bmp.size();
  }
  for (auto& e : entries)
    for (uint8_t v : e.bmp) ico.push_back(v);

  std::ofstream f(out, std::ios::binary);
  f.write((const char*)ico.data(), ico.size());
  f.close();
  printf("wrote %s (%zu bytes)\n", out, ico.size());
  return f ? 0 : 1;
}
