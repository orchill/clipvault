// history.h — clipboard history model: ordering, dedup, pinning, limits, search view.
#pragma once
#include "settings.h"
#include "util.h"

namespace cv {

enum class ItemType { Text = 0, Rtf, Html, Image, Files };

const wchar_t* ItemTypeName(ItemType t);

// One clipboard entry. Serialized by storage; `thumb` is runtime-only.
struct Item {
  i64 id = 0;
  u64 hash = 0;              // content hash (dedup key)
  ItemType type = ItemType::Text;

  wstring text;              // preview + search text (capped at kTextRamCap chars)
  wstring textLower;         // ToLowerW(text), avoids re-lowercasing per keystroke
  string blobFile;           // blobs\<hash>.txt / .rtf / .html ("" = none)
  string imgFile;            // blobs\<hash>.png / .jpg ("" = none)
  bool imgJpeg = false;
  int imgW = 0, imgH = 0;
  u64 ts = 0;                // last-copied time (ms epoch)
  u64 size = 0;              // approximate payload size in bytes
  bool pinned = false;
  wstring srcApp;            // source process (no .exe)

  void* thumb = nullptr;     // Gdiplus::Bitmap* — void* to avoid including gdiplus here
  bool thumbPending = false; // transient: thumbnail regeneration queued

  u64 DisplaySize() const;
  // text stored in RAM per item; full text above this goes to a blob file
  static constexpr size_t kTextRamCap = 65536;
};

class History {
 public:
  // returns: 1 = added, 0 = existing item deduplicated/refreshed, -1 = rejected
  int Add(Item&& it);
  bool Remove(i64 id);
  bool SetPinned(i64 id, bool pin);
  void ClearUnpinned();
  void ClearAll();
  void AutoClean(int days);
  void EnforceImageBudget(u64 maxBytes);

  Item* Find(i64 id);
  Item* FindHash(u64 h);
  size_t PinnedCount() const { return pinnedCount_; }
  size_t UnpinnedCount() const { return items_.size() - pinnedCount_; }
  size_t TotalCount() const { return items_.size(); }

  // Search + type filter (event-driven: UI calls this only on keystroke/filter change).
  // filter: 0=All 1=Text 2=Images 3=Pinned
  void BuildView(const wstring& searchLower, int filter, std::vector<Item*>& out);

  std::vector<Item>& Items() { return items_; }  // storage iteration (save)
  void AdoptLoaded(std::vector<Item>&& loaded);  // after storage load

 private:
  std::vector<Item> items_;  // pinned first (ts desc), then unpinned (ts desc)
  size_t pinnedCount_ = 0;

  void PruneToLimit();
  // Deletes every file in `names` that no remaining item references.
  // Callers must already have removed the owning items from items_.
  void GcUnreferenced(const std::vector<string>& names);
  void DeleteBlobsIfUnreferenced(const string& blobFile, const string& imgFile);
  void UnlinkAt(size_t idx);
};

}  // namespace cv
