// util.h — shared helpers: text conversion, hashing, JSON, file IO.
// ClipVault is in the public domain / MIT — see README.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace cv {

using std::string;
using std::wstring;
using u8 = uint8_t;
using u32 = uint32_t;
using u64 = uint64_t;
using i64 = int64_t;

// ---------- text ----------
wstring Utf8ToUtf16(const char* s, size_t len);
inline wstring Utf8ToUtf16(const string& s) { return Utf8ToUtf16(s.data(), s.size()); }
string Utf16ToUtf8(const wchar_t* s, size_t len);
inline string Utf16ToUtf8(const wstring& s) { return Utf16ToUtf8(s.data(), s.size()); }

wstring ToLowerW(const wstring& s);
inline bool ContainsICW(const wstring& hay, const wstring& needleLower) {
  return ToLowerW(hay).find(needleLower) != wstring::npos; // simple; calls are rare (keystrokes only)
}

// ---------- hashing (FNV-1a 64) ----------
u64 Fnv64(const void* data, size_t len);
inline u64 Fnv64Combine(u64 h, u64 v) { h ^= (u64)v; h *= 0x100000001b3ULL; return h; }
string HashToHex(u64 h);

// ---------- misc ----------
u64 NowMs();
wstring FormatBytes(u64 b);
wstring RelativeTime(u64 tsMs);
wstring BaseNameW(const wstring& path);
bool FileExistsW(const wstring& path);
u64 FileSizeW(const wstring& path);
bool ReadAllBytes(const wstring& path, std::vector<u8>& out);
bool WriteAllBytesAtomic(const wstring& path, const void* data, size_t len);
bool DeleteFileQuiet(const wstring& path);
wstring ExePath();
wstring DataDir();     // %LOCALAPPDATA%\ClipVault (created on demand)
wstring BlobDir();     // DataDir()\blobs
std::vector<u8> ReadResource(int id, const wchar_t* type);

// ---------- minimal JSON (DOM) ----------
class Json {
 public:
  enum T { NUL, BOOLV, INT, DBL, STR, ARR, OBJ };
  T t = NUL;
  bool b = false;
  i64 i = 0;
  double d = 0.0;
  wstring s;
  std::vector<Json> a;
  std::vector<std::pair<wstring, Json>> o;

  static Json Obj() { Json j; j.t = OBJ; return j; }
  static Json Arr() { Json j; j.t = ARR; return j; }
  static Json S(wstring v) { Json j; j.t = STR; j.s = std::move(v); return j; }
  static Json I(i64 v) { Json j; j.t = INT; j.i = v; return j; }
  static Json B(bool v) { Json j; j.t = BOOLV; j.b = v; return j; }

  Json& set(const wchar_t* k, Json v) { o.emplace_back(k, std::move(v)); return *this; }
  const Json* get(const wchar_t* k) const {
    if (t != OBJ) return nullptr;
    for (auto& kv : o)
      if (wcscmp(kv.first.c_str(), k) == 0) return &kv.second;
    return nullptr;
  }
  // value-based accessors (call on the value node)
  i64 asInt(i64 def) const {
    if (t == INT) return i;
    if (t == DBL) return (i64)d;
    return def;
  }
  bool asBool(bool def) const { return t == BOOLV ? b : def; }
  wstring asStr(const wchar_t* def) const { return t == STR ? s : wstring(def); }
  // key-based accessors (call on the object node; missing key -> default)
  i64 asInt(const wchar_t* key, i64 def) const {
    const Json* v = get(key);
    return v ? v->asInt(def) : def;
  }
  bool asBool(const wchar_t* key, bool def) const {
    const Json* v = get(key);
    return v ? v->asBool(def) : def;
  }
  wstring asStr(const wchar_t* key, const wchar_t* def) const {
    const Json* v = get(key);
    return v ? v->asStr(def) : wstring(def);
  }

  string dump() const;
  void dumpTo(string& out) const;
  static bool parse(const char* data, size_t len, Json& out);
};

}  // namespace cv
