#include "util.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cstdio>

namespace cv {

// ---------------- text ----------------
wstring Utf8ToUtf16(const char* s, size_t len) {
  if (!s || !len) return wstring();
  int n = MultiByteToWideChar(CP_UTF8, 0, s, (int)len, nullptr, 0);
  if (n <= 0) return wstring();
  wstring out((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s, (int)len, &out[0], n);
  return out;
}

string Utf16ToUtf8(const wchar_t* s, size_t len) {
  if (!s || !len) return string();
  int n = WideCharToMultiByte(CP_UTF8, 0, s, (int)len, nullptr, 0, nullptr, nullptr);
  if (n <= 0) return string();
  string out((size_t)n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, s, (int)len, &out[0], n, nullptr, nullptr);
  return out;
}

wstring ToLowerW(const wstring& s) {
  wstring out = s;
  if (!out.empty()) CharLowerBuffW(&out[0], (DWORD)out.size());
  return out;
}

// ---------------- hashing ----------------
u64 Fnv64(const void* data, size_t len) {
  const u8* p = (const u8*)data;
  u64 h = 0xcbf29ce484222325ULL;
  for (size_t k = 0; k < len; k++) { h ^= p[k]; h *= 0x100000001b3ULL; }
  return h;
}

string HashToHex(u64 h) {
  char buf[32];
  snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)h);
  return buf;
}

// ---------------- misc ----------------
u64 NowMs() {
  FILETIME ft;
  GetSystemTimeAsFileTime(&ft);
  ULARGE_INTEGER li;
  li.LowPart = ft.dwLowDateTime;
  li.HighPart = ft.dwHighDateTime;
  return (u64)(li.QuadPart / 10000ULL);  // 100ns -> ms
}

wstring FormatBytes(u64 b) {
  wchar_t buf[64];
  if (b < 1024) swprintf(buf, 64, L"%llu B", (unsigned long long)b);
  else if (b < 1024 * 1024) swprintf(buf, 64, L"%.1f KB", b / 1024.0);
  else swprintf(buf, 64, L"%.1f MB", b / (1024.0 * 1024.0));
  // trim trailing ".0"
  wstring s = buf;
  size_t p = s.find(L".0 ");
  if (p != wstring::npos && p + 3 == s.size()) s.erase(p, 2);
  return s;
}

wstring RelativeTime(u64 tsMs) {
  u64 now = NowMs();
  i64 dms = (i64)now - (i64)tsMs;
  if (dms < 0) dms = 0;
  i64 sec = dms / 1000;
  wchar_t buf[64];
  if (sec < 60) return L"now";
  i64 min = sec / 60;
  if (min < 60) { swprintf(buf, 64, L"%lldm", (long long)min); return buf; }
  i64 hr = min / 60;
  if (hr < 24) { swprintf(buf, 64, L"%lldh", (long long)hr); return buf; }
  i64 day = hr / 24;
  if (day < 7) { swprintf(buf, 64, L"%lldd", (long long)day); return buf; }
  // absolute date for older items
  FILETIME ft;
  ULARGE_INTEGER li;
  li.QuadPart = (u64)tsMs * 10000ULL;
  ft.dwLowDateTime = li.LowPart;
  ft.dwHighDateTime = li.HighPart;
  SYSTEMTIME st;
  FileTimeToSystemTime(&ft, &st);
  swprintf(buf, 64, L"%d/%02d", st.wMonth, st.wDay);
  return buf;
}

wstring BaseNameW(const wstring& path) {
  size_t p = path.find_last_of(L"\\/");
  return p == wstring::npos ? path : path.substr(p + 1);
}

bool FileExistsW(const wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

u64 FileSizeW(const wstring& path) {
  WIN32_FILE_ATTRIBUTE_DATA fa;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa)) return 0;
  ULARGE_INTEGER li;
  li.LowPart = fa.nFileSizeLow;
  li.HighPart = fa.nFileSizeHigh;
  return li.QuadPart;
}

bool ReadAllBytes(const wstring& path, std::vector<u8>& out) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                         OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  bool ok = false;
  if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 0 && sz.QuadPart < (LONGLONG)1 << 31) {
    out.resize((size_t)sz.QuadPart);
    DWORD got = 0;
    ok = out.empty() ||
         (ReadFile(h, out.data(), (DWORD)out.size(), &got, nullptr) && got == out.size());
  }
  CloseHandle(h);
  if (!ok) out.clear();
  return ok;
}

bool WriteAllBytesAtomic(const wstring& path, const void* data, size_t len) {
  wstring tmp = path + L".tmp";
  HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  bool ok = true;
  if (len) {
    DWORD put = 0;
    ok = WriteFile(h, data, (DWORD)len, &put, nullptr) && put == len;
  }
  if (ok) ok = FlushFileBuffers(h) != 0;
  CloseHandle(h);
  if (!ok) { DeleteFileW(tmp.c_str()); return false; }
  if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    DeleteFileW(tmp.c_str());
    return false;
  }
  return true;
}

bool DeleteFileQuiet(const wstring& path) { return DeleteFileW(path.c_str()) != 0; }

wstring ExePath() {
  wchar_t buf[MAX_PATH + 32];
  DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(wchar_t)));
  return wstring(buf, n);
}

static void EnsureDir(const wstring& dir) { CreateDirectoryW(dir.c_str(), nullptr); }

wstring DataDir() {
  PWSTR raw = nullptr;
  wstring base;
  if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) {
    base = raw;
    CoTaskMemFree(raw);
  } else {
    base = L"C:\\ProgramData";
  }
  wstring dir = base + L"\\ClipVault";
  EnsureDir(dir);
  wstring blobs = dir + L"\\blobs";
  EnsureDir(blobs);
  return dir;
}

wstring BlobDir() { return DataDir() + L"\\blobs"; }

std::vector<u8> ReadResource(int id, const wchar_t* type) {
  HRSRC rc = FindResourceW(nullptr, MAKEINTRESOURCEW(id), type);
  if (!rc) return {};
  HGLOBAL h = LoadResource(nullptr, rc);
  if (!h) return {};
  const void* p = LockResource(h);
  DWORD sz = SizeofResource(nullptr, rc);
  if (!p || !sz) return {};
  const u8* bp = (const u8*)p;
  return std::vector<u8>(bp, bp + sz);
}

// ---------------- JSON ----------------
string Json::dump() const {
  string out;
  dumpTo(out);
  return out;
}

namespace {

void DumpString(const wstring& w, string& out) {
  out += '"';
  string u8s = Utf16ToUtf8(w);
  for (size_t k = 0; k < u8s.size(); k++) {
    unsigned char c = (unsigned char)u8s[k];
    switch (c) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      case '\b': out += "\\b"; break;
      case '\f': out += "\\f"; break;
      default:
        if (c < 0x20) {
          char buf[8];
          snprintf(buf, sizeof(buf), "\\u%04x", c);
          out += buf;
        } else {
          out += (char)c;
        }
    }
  }
  out += '"';
}

}  // namespace

void Json::dumpTo(string& out) const {
  switch (t) {
    case NUL: out += "null"; break;
    case BOOLV: out += b ? "true" : "false"; break;
    case INT: {
      char buf[32];
      snprintf(buf, sizeof(buf), "%lld", (long long)i);
      out += buf;
      break;
    }
    case DBL: {
      char buf[40];
      snprintf(buf, sizeof(buf), "%.6g", d);
      out += buf;
      break;
    }
    case STR: DumpString(s, out); break;
    case ARR: {
      out += '[';
      for (size_t k = 0; k < a.size(); k++) {
        if (k) out += ',';
        a[k].dumpTo(out);
      }
      out += ']';
      break;
    }
    case OBJ: {
      out += '{';
      for (size_t k = 0; k < o.size(); k++) {
        if (k) out += ',';
        DumpString(o[k].first, out);
        out += ':';
        o[k].second.dumpTo(out);
      }
      out += '}';
      break;
    }
  }
}

namespace {

struct Parser {
  const char* p;
  const char* end;
  bool fail = false;

  void ws() {
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++;
  }
  bool eat(char c) {
    ws();
    if (p < end && *p == c) { p++; return true; }
    return false;
  }
  bool peek(char c) { ws(); return p < end && *p == c; }

  bool parseValue(Json& out) {
    ws();
    if (p >= end) return false;
    char c = *p;
    if (c == '{') return parseObj(out);
    if (c == '[') return parseArr(out);
    if (c == '"') { out.t = Json::STR; return parseStr(out.s); }
    if (c == 't') return lit("true", [&] { out.t = Json::BOOLV; out.b = true; });
    if (c == 'f') return lit("false", [&] { out.t = Json::BOOLV; out.b = false; });
    if (c == 'n') return lit("null", [&] { out.t = Json::NUL; });
    return parseNum(out);
  }

  template <typename F>
  bool lit(const char* word, F&& set) {
    size_t n = strlen(word);
    if ((size_t)(end - p) >= n && memcmp(p, word, n) == 0) {
      p += n;
      set();
      return true;
    }
    fail = true;
    return false;
  }

  bool parseNum(Json& out) {
    ws();
    char buf[64];
    size_t n = 0;
    bool isInt = true;
    while (p < end && n < 60) {
      char c = *p;
      if ((c >= '0' && c <= '9') || c == '-') { buf[n++] = *p++; }
      else if (c == '.' || c == 'e' || c == 'E' || c == '+') { isInt = false; buf[n++] = *p++; }
      else break;
    }
    if (!n) { fail = true; return false; }
    buf[n] = 0;
    if (isInt) {
      out.t = Json::INT;
      out.i = _atoi64(buf);
    } else {
      out.t = Json::DBL;
      out.d = atof(buf);
    }
    return true;
  }

  bool parseStr(wstring& out) {
    if (p >= end || *p != '"') { fail = true; return false; }
    p++;
    string u8;
    while (p < end) {
      char c = *p;
      if (c == '"') { p++; out = Utf8ToUtf16(u8); return true; }
      if (c == '\\') {
        p++;
        if (p >= end) break;
        char e = *p++;
        switch (e) {
          case '"': u8 += '"'; break;
          case '\\': u8 += '\\'; break;
          case '/': u8 += '/'; break;
          case 'n': u8 += '\n'; break;
          case 'r': u8 += '\r'; break;
          case 't': u8 += '\t'; break;
          case 'b': u8 += '\b'; break;
          case 'f': u8 += '\f'; break;
          case 'u': {
            if (end - p < 4) { fail = true; return false; }
            wchar_t wc = 0;
            for (int k = 0; k < 4; k++) {
              char h = *p++;
              wc <<= 4;
              if (h >= '0' && h <= '9') wc |= h - '0';
              else if (h >= 'a' && h <= 'f') wc |= h - 'a' + 10;
              else if (h >= 'A' && h <= 'F') wc |= h - 'A' + 10;
              else { fail = true; return false; }
            }
            // encode as utf8
            if (wc < 0x80) u8 += (char)wc;
            else if (wc < 0x800) {
              u8 += (char)(0xC0 | (wc >> 6));
              u8 += (char)(0x80 | (wc & 0x3F));
            } else {
              u8 += (char)(0xE0 | (wc >> 12));
              u8 += (char)(0x80 | ((wc >> 6) & 0x3F));
              u8 += (char)(0x80 | (wc & 0x3F));
            }
            break;
          }
          default: fail = true; return false;
        }
      } else {
        u8 += c;
        p++;
      }
    }
    fail = true;
    return false;
  }

  bool parseArr(Json& out) {
    out.t = Json::ARR;
    if (!eat('[')) { fail = true; return false; }
    if (eat(']')) return true;
    while (true) {
      Json v;
      if (!parseValue(v)) return false;
      out.a.push_back(std::move(v));
      if (eat(']')) return true;
      if (!eat(',')) { fail = true; return false; }
    }
  }

  bool parseObj(Json& out) {
    out.t = Json::OBJ;
    if (!eat('{')) { fail = true; return false; }
    if (eat('}')) return true;
    while (true) {
      wstring key;
      if (!peek('"') || !parseStr(key)) { fail = true; return false; }
      if (!eat(':')) { fail = true; return false; }
      Json v;
      if (!parseValue(v)) return false;
      out.o.emplace_back(std::move(key), std::move(v));
      if (eat('}')) return true;
      if (!eat(',')) { fail = true; return false; }
    }
  }
};

}  // namespace

bool Json::parse(const char* data, size_t len, Json& out) {
  Parser ps{data, data + len};
  if (!ps.parseValue(out)) return false;
  ps.ws();
  return ps.p == ps.end && !ps.fail;
}

}  // namespace cv
