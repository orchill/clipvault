#include "storage.h"

#include <shellapi.h>

namespace cv {

static wstring ItemsPath() { return DataDir() + L"\\items.json"; }

static Json SerializeItem(const Item& it) {
  Json j = Json::Obj();
  j.set(L"id", Json::I(it.id));
  j.set(L"hash", Json::S(Utf8ToUtf16(HashToHex(it.hash))));
  j.set(L"type", Json::I((i64)it.type));
  j.set(L"text", Json::S(it.text));
  j.set(L"blobFile", Json::S(Utf8ToUtf16(it.blobFile)));
  j.set(L"imgFile", Json::S(Utf8ToUtf16(it.imgFile)));
  j.set(L"imgJpeg", Json::B(it.imgJpeg));
  j.set(L"w", Json::I(it.imgW));
  j.set(L"h", Json::I(it.imgH));
  j.set(L"ts", Json::I((i64)it.ts));
  j.set(L"size", Json::I((i64)it.size));
  j.set(L"pinned", Json::B(it.pinned));
  j.set(L"src", Json::S(it.srcApp));
  return j;
}

static bool ParseItem(const Json& j, Item& it) {
  it.id = j.asInt(L"id", 0);
  wstring hex = j.asStr(L"hash", L"");
  if (hex.size() == 16) it.hash = _strtoui64(Utf16ToUtf8(hex).c_str(), nullptr, 16);
  it.type = (ItemType)j.asInt(L"type", 0);
  it.text = j.asStr(L"text", L"");
  it.blobFile = Utf16ToUtf8(j.asStr(L"blobFile", L""));
  it.imgFile = Utf16ToUtf8(j.asStr(L"imgFile", L""));
  it.imgJpeg = j.asBool(L"imgJpeg", false);
  it.imgW = (int)j.asInt(L"w", 0);
  it.imgH = (int)j.asInt(L"h", 0);
  it.ts = (u64)j.asInt(L"ts", 0);
  it.size = (u64)j.asInt(L"size", 0);
  it.pinned = j.asBool(L"pinned", false);
  it.srcApp = j.asStr(L"src", L"");
  it.textLower = ToLowerW(it.text);
  return it.id != 0 && it.hash != 0;
}

void Storage::Load(History& hist) {
  Settings& s = Settings::I();
  if (!s.persist) return;  // persistence disabled -> RAM-only session

  std::vector<u8> raw;
  if (!ReadAllBytes(ItemsPath(), raw) || raw.empty()) return;

  // tolerate a UTF-8 BOM (e.g. files edited in Notepad)
  const char* jdata = (const char*)raw.data();
  size_t jlen = raw.size();
  if (jlen >= 3 && (u8)jdata[0] == 0xEF && (u8)jdata[1] == 0xBB && (u8)jdata[2] == 0xBF) {
    jdata += 3;
    jlen -= 3;
  }
  Json root;
  if (!Json::parse(jdata, jlen, root)) {
    // quarantine corrupt file, start fresh (spec: handle corruption gracefully)
    wstring dead = ItemsPath() + L".corrupt." + std::to_wstring(GetTickCount64());
    MoveFileExW(ItemsPath().c_str(), dead.c_str(), MOVEFILE_REPLACE_EXISTING);
    return;
  if (root.get(L"version") && root.get(L"version")->asInt(0) != 1) {
    // unknown schema: quarantine like corruption rather than mis-parsing
    wstring deadv = ItemsPath() + L".unknown." + std::to_wstring(GetTickCount64());
    MoveFileExW(ItemsPath().c_str(), deadv.c_str(), MOVEFILE_REPLACE_EXISTING);
    return;
  }
  }

  std::vector<Item> loaded;
  if (const Json* arr = root.get(L"items"); arr && arr->t == Json::ARR) {
    for (auto& v : arr->a) {
      Item it;
      if (ParseItem(v, it)) {
        // tolerate blobs that disappeared on disk
        if (!it.blobFile.empty() &&
            !FileExistsW(BlobDir() + L"\\" + Utf8ToUtf16(it.blobFile)))
          it.blobFile.clear();
        if (!it.imgFile.empty() && !FileExistsW(BlobDir() + L"\\" + Utf8ToUtf16(it.imgFile))) {
          it.imgFile.clear();
          it.imgW = it.imgH = 0;
        }
        loaded.push_back(std::move(it));
      }
    }
  }
  hist.AdoptLoaded(std::move(loaded));
}

bool Storage::Save(History& hist) {
  Settings& s = Settings::I();
  if (!s.persist) return true;  // nothing to write (disk economy)

  Json root = Json::Obj();
  root.set(L"version", Json::I(1));
  Json arr = Json::Arr();
  for (auto& it : hist.Items()) arr.a.push_back(SerializeItem(it));
  root.set(L"items", std::move(arr));
  string out = root.dump();
  return WriteAllBytesAtomic(ItemsPath(), out.data(), out.size());
}

}  // namespace cv
