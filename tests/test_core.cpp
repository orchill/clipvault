// test_core.cpp — deterministic regression tests for the audit fixes.
// Linked against the app's object files; exits non-zero on any failure.
#include "../src/history.h"
#include "../src/imaging.h"
#include "../src/settings.h"
#include "../src/util.h"

#include <cstdio>
#include <filesystem>

using namespace cv;

static int g_failed = 0;
#define CHECK(cond)                                                       \
  do {                                                                    \
    if (cond) printf("  PASS: %s\n", #cond);                              \
    else { printf("  FAIL: %s (line %d)\n", #cond, __LINE__); g_failed++; } \
  } while (0)

static std::wstring blobPath(const std::string& name) {
  return BlobDir() + L"\\" + Utf8ToUtf16(name);
}
static bool blobExists(const std::string& name) {
  return !name.empty() && FileExistsW(blobPath(name));
}
static Item makeImageItem(const char* hashSeed, u64 bytes) {
  Item it;
  it.id = (i64)NowMs() * 1000 + rand() % 1000;
  it.hash = Fnv64(hashSeed, strlen(hashSeed));
  it.type = ItemType::Image;
  it.imgFile = HashToHex(it.hash) + ".png";
  it.imgW = 640;
  it.imgH = 480;
  it.ts = NowMs();
  it.size = bytes;
  WriteAllBytesAtomic(blobPath(it.imgFile), hashSeed, strlen(hashSeed));
  return it;
}
static Item makeTextItem(const char* hashSeed, const wchar_t* text, bool spill) {
  Item it;
  it.id = (i64)NowMs() * 1000 + rand() % 1000;
  it.hash = Fnv64(hashSeed, strlen(hashSeed));
  it.type = ItemType::Text;
  it.ts = NowMs();
  it.text = text;
  it.textLower = ToLowerW(it.text);
  if (spill) {
    it.blobFile = HashToHex(it.hash) + ".txt";
    WriteAllBytesAtomic(blobPath(it.blobFile), hashSeed, strlen(hashSeed));
  }
  return it;
}

static void testGc() {
  printf("== blob GC ==\n");
  History h;
  Item a = makeImageItem("image-A", 1000);
  std::string blobA = a.imgFile;
  CHECK(h.Add(std::move(a)) == 1);
  CHECK(blobExists(blobA));

  // 1. delete item -> blob disappears
  Item* id = h.FindHash(Fnv64("image-A", 7));
  i64 idA = id->id;
  CHECK(h.Remove(idA));
  CHECK(!blobExists(blobA));

  // 2. duplicate image -> shared blob survives one delete
  Item b1 = makeImageItem("image-B", 2000);
  std::string blobB = b1.imgFile;
  h.Add(std::move(b1));
  Item b2 = makeImageItem("image-B", 2000);  // same content -> same hash/blob
  b2.id = (i64)NowMs() * 1000 + 7;
  h.Add(std::move(b2));                       // dedup -> still ONE item
  CHECK(h.TotalCount() == 1);
  CHECK(blobExists(blobB));
  CHECK(h.Remove(h.FindHash(Fnv64("image-B", 7))->id));
  CHECK(!blobExists(blobB));

  // 3. prune through the limit -> oldest blob disappears
  Settings::I().maxItems = 5;
  std::string oldest;
  for (int k = 0; k < 6; k++) {
    char seed[32];
    snprintf(seed, 32, "prune-%d", k);
    Item it = makeImageItem(seed, 3000 + k);
    if (k == 0) oldest = it.imgFile;
    h.Add(std::move(it));
  }
  CHECK(h.UnpinnedCount() == 5);
  CHECK(!blobExists(oldest));          // pruned by the limit
  CHECK(blobExists(HashToHex(Fnv64("prune-1", 7)) + ".png"));  // newer survive

  // 4. clear unpinned -> blobs disappear
  h.ClearUnpinned();
  CHECK(h.UnpinnedCount() == 0);
  CHECK(!blobExists(HashToHex(Fnv64("prune-5", 7)) + ".png"));

  // 5. pinned image survives clear unpinned
  Settings::I().maxItems = 25;
  Item p = makeImageItem("pinned-img", 4000);
  std::string blobP = p.imgFile;
  h.Add(std::move(p));
  CHECK(h.SetPinned(h.FindHash(Fnv64("pinned-img", 10))->id, true));
  Item q = makeImageItem("unpinned-img", 5000);
  std::string blobQ = q.imgFile;
  h.Add(std::move(q));
  h.ClearUnpinned();
  CHECK(blobExists(blobP));    // pinned: must remain
  CHECK(!blobExists(blobQ));   // unpinned: must go
  CHECK(h.PinnedCount() == 1);

  // 6. clear all -> everything unreferenced goes
  h.ClearAll();
  CHECK(!blobExists(blobP));
  CHECK(h.TotalCount() == 0);

  // 7. large spilled text blobs behave the same
  Item t = makeTextItem("spill-A", L"big", true);
  std::string blobT = t.blobFile;
  h.Add(std::move(t));
  CHECK(blobExists(blobT));
  CHECK(h.Remove(h.FindHash(Fnv64("spill-A", 7))->id));
  CHECK(!blobExists(blobT));
  Settings::I().maxItems = 25;
}

static void testDib() {
  printf("== DIB bounds safety ==\n");
  // well-formed 32bpp bottom-up 4x3
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 4;
    bi.biHeight = 3;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi));
    std::vector<u8> bits(4 * 4 * 3, 0xAB);
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(DibToBgra(header, bits, true, bgra, w, h));
    CHECK(w == 4 && h == 3 && bgra.size() == 4 * 4 * 3);
  }
  // truncated 32bpp: header claims 4x3, buffer holds only 2 rows
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 4;
    bi.biHeight = 3;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi));
    std::vector<u8> bits(4 * 4 * 2, 0xAB);  // truncated
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(!DibToBgra(header, bits, true, bgra, w, h));
    CHECK(bgra.empty());
  }
  // truncated 24bpp top-down
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 4;
    bi.biHeight = -3;  // top-down
    bi.biPlanes = 1;
    bi.biBitCount = 24;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi));
    std::vector<u8> bits(4 * 3 * 1, 0xAB);  // one row instead of three
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(!DibToBgra(header, bits, false, bgra, w, h));
  }
  // nonsense dimensions
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 0;
    bi.biHeight = 5;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi));
    std::vector<u8> bits(64, 0);
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(!DibToBgra(header, bits, true, bgra, w, h));
  }
  // huge dimensions that would overflow 32-bit math: must be rejected, not crash
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 0x40000000;  // ~1 billion
    bi.biHeight = 0x40000000;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi));
    std::vector<u8> bits(64, 0);
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(!DibToBgra(header, bits, true, bgra, w, h));
  }
  // 8bpp (exotic path through GDI) with truncated buffer
  {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = 8;
    bi.biHeight = 8;
    bi.biPlanes = 1;
    bi.biBitCount = 8;
    bi.biClrUsed = 2;
    std::vector<u8> header((u8*)&bi, (u8*)&bi + sizeof(bi) + 2 * sizeof(RGBQUAD));
    std::vector<u8> bits(8, 0);  // truncated (needs stride*8 = 64)
    std::vector<u8> bgra;
    int w = 0, h = 0;
    CHECK(!DibToBgra(header, bits, false, bgra, w, h));
  }
}

static void testHistoryEdges() {
  printf("== history edge cases ==\n");
  History h;
  Settings::I().maxItems = 3;
  Settings::I().dupMode = (int)DupMode::MoveTop;

  // all pinned + new item: nothing removed, new item still added
  for (int k = 0; k < 3; k++) {
    char seed[16];
    snprintf(seed, 16, "pin-%d", k);
    Item it = makeImageItem(seed, 1);
    h.Add(std::move(it));
    h.SetPinned(h.FindHash(Fnv64(seed, strlen(seed)))->id, true);
  }
  Item extra = makeImageItem("extra", 9);
  std::string blobExtra = extra.imgFile;
  CHECK(h.Add(std::move(extra)) == 1);
  CHECK(h.PinnedCount() == 3 && h.UnpinnedCount() == 1);
  CHECK(blobExists(blobExtra));

  // duplicate of a pinned item: stays pinned, moves nothing, no new entry
  Item dup = makeImageItem("pin-1", 1);
  CHECK(h.Add(std::move(dup)) == 0);
  CHECK(h.PinnedCount() == 3 && h.UnpinnedCount() == 1);

  // unpin -> dedup MoveTop: unpinned duplicate goes to newest unpinned slot
  Item* pinnedDup = h.FindHash(Fnv64("pin-1", 5));
  h.SetPinned(pinnedDup->id, false);
  Item dup2 = makeImageItem("pin-1", 1);
  CHECK(h.Add(std::move(dup2)) == 0);