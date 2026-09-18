#include "history.h"

#include <objidl.h>
#include <algorithm>
#include <gdiplus.h>

namespace cv {

const wchar_t* ItemTypeName(ItemType t) {
  switch (t) {
    case ItemType::Text: return L"Text";
    case ItemType::Rtf: return L"Rich text";
    case ItemType::Html: return L"HTML";
    case ItemType::Image: return L"Image";
    case ItemType::Files: return L"Files";
  }
  return L"";
}

u64 Item::DisplaySize() const {
  if (size) return size;
  return text.size() * 2;
}

static void FreeThumb(Item& it) {
  if (it.thumb) {
    delete (Gdiplus::Bitmap*)it.thumb;
    it.thumb = nullptr;
  }
}

Item* History::Find(i64 id) {
  for (auto& it : items_)
    if (it.id == id) return &it;
  return nullptr;
}

Item* History::FindHash(u64 h) {
  for (auto& it : items_)
    if (it.hash == h) return &it;
  return nullptr;
}

int History::Add(Item&& it) {
  Settings& s = Settings::I();
  Item* ex = FindHash(it.hash);
  if (ex && s.dupMode != (int)DupMode::Allow) {
    if (s.dupMode == (int)DupMode::KeepPos) {
      // keep the existing timestamp so the entry keeps its position on reload
    } else {
      ex->ts = it.ts;
      ex->size = it.size ? it.size : ex->size;
    }
    if (s.dupMode == (int)DupMode::MoveTop && !ex->pinned && pinnedCount_ > 0) {
      // move to newest unpinned position (front of the unpinned section)
      size_t idx = (size_t)(ex - items_.data());
      Item moved = std::move(*ex);
      items_.erase(items_.begin() + idx);
      items_.insert(items_.begin() + pinnedCount_, std::move(moved));
    }
    return 0;
  }

  if (UnpinnedCount() + 1 > (size_t)s.maxItems) {
    // History full: spec requires removing the OLDEST UNPINNED item first and
    // never deleting pinned items. If every entry is pinned, nothing is removed.
    if (UnpinnedCount() > 0) {
      size_t victim = items_.size() - 1;  // back of unpinned section = oldest
      string blobFile = items_[victim].blobFile;
      string imgFile = items_[victim].imgFile;
      FreeThumb(items_[victim]);
      items_.pop_back();
      DeleteBlobsIfUnreferenced(blobFile, imgFile);  // after removal: refs are honest
    }
  }

  items_.insert(items_.begin() + pinnedCount_, std::move(it));
  return 1;
}

void History::UnlinkAt(size_t idx) {
  string blobFile = items_[idx].blobFile;
  string imgFile = items_[idx].imgFile;
  FreeThumb(items_[idx]);
  if (idx < pinnedCount_) pinnedCount_--;
  items_.erase(items_.begin() + idx);
  DeleteBlobsIfUnreferenced(blobFile, imgFile);  // after removal: refs are honest
}

bool History::Remove(i64 id) {
  for (size_t k = 0; k < items_.size(); k++) {
    if (items_[k].id == id) {
      UnlinkAt(k);
      return true;
    }
  }
  return false;
}

bool History::SetPinned(i64 id, bool pin) {
  for (size_t k = 0; k < items_.size(); k++) {
    if (items_[k].id == id && items_[k].pinned != pin) {
      items_[k].pinned = pin;
      Item moved = std::move(items_[k]);
      items_.erase(items_.begin() + k);
      if (pin) {
        // front of pinned section, newest pin first
        items_.insert(items_.begin(), std::move(moved));
        pinnedCount_++;
      } else {
        pinnedCount_--;  // the item leaves the pinned section
        items_.insert(items_.begin() + pinnedCount_, std::move(moved));  // top of unpinned
      }
      return true;
    }
  }
  return false;
}

void History::ClearUnpinned() {
  // collect affected blob names first; GC only after the items are gone so
  // reference counts are honest (blobs are shared between duplicate items)
  std::vector<string> names;
  for (size_t k = items_.size(); k-- > pinnedCount_;) {
    names.push_back(items_[k].blobFile);
    names.push_back(items_[k].imgFile);
    FreeThumb(items_[k]);
  }
  items_.resize(pinnedCount_);
  GcUnreferenced(names);
}

void History::ClearAll() {
  std::vector<string> names;
  for (auto& it : items_) {
    names.push_back(it.blobFile);
    names.push_back(it.imgFile);
    FreeThumb(it);
  }
  items_.clear();
  pinnedCount_ = 0;
  GcUnreferenced(names);
}

void History::AutoClean(int days) {
  if (days <= 0) return;
  u64 cutoff = NowMs() - (u64)days * 24ULL * 3600ULL * 1000ULL;
  for (size_t k = items_.size(); k-- > pinnedCount_;) {
    if (items_[k].ts < cutoff) UnlinkAt(k);
  }
}

void History::EnforceImageBudget(u64 maxBytes) {
  u64 total = 0;
  for (auto& it : items_)
    if (it.imgFile.empty() == false) total += it.size;
  if (total <= maxBytes) return;
  for (size_t k = items_.size(); k-- > pinnedCount_ && total > maxBytes;) {
    if (!items_[k].imgFile.empty()) {
      total -= items_[k].size;
      UnlinkAt(k);
    }
  }
}

void History::DeleteBlobsIfUnreferenced(const string& blobFile, const string& imgFile) {
  // callers must have removed the owning item already; zero remaining
  // references means the content-addressed blob is safe to delete
  GcUnreferenced({blobFile, imgFile});
}

void History::GcUnreferenced(const std::vector<string>& names) {
  // dedup first: several removed items may have shared one blob file
  std::vector<string> uniq = names;
  std::sort(uniq.begin(), uniq.end());
  uniq.erase(std::unique(uniq.begin(), uniq.end()), uniq.end());
  for (auto& f : uniq) {
    if (f.empty()) continue;
    bool referenced = false;
    for (auto& it : items_)
      if (it.blobFile == f || it.imgFile == f) { referenced = true; break; }
    if (!referenced) DeleteFileQuiet(BlobDir() + L"\\" + Utf8ToUtf16(f));
  }
}

void History::BuildView(const wstring& searchLower, int filter, std::vector<Item*>& out) {
  out.clear();
  bool searching = !searchLower.empty();
  for (auto& it : items_) {
    if (filter == 1 && (it.type != ItemType::Text && it.type != ItemType::Rtf &&
                        it.type != ItemType::Html && it.type != ItemType::Files))
      continue;
    if (filter == 2 && it.type != ItemType::Image) continue;
    if (filter == 3 && !it.pinned) continue;
    if (searching && it.textLower.find(searchLower) == wstring::npos) continue;
    out.push_back(&it);
  }
}

void History::AdoptLoaded(std::vector<Item>&& loaded) {
  items_ = std::move(loaded);
  pinnedCount_ = 0;
  while (pinnedCount_ < items_.size() && items_[pinnedCount_].pinned) pinnedCount_++;
  // normalize: pinned (ts desc) then unpinned (ts desc)
  std::stable_sort(items_.begin(), items_.begin() + pinnedCount_,
                   [](const Item& a, const Item& b) { return a.ts > b.ts; });
  std::stable_sort(items_.begin() + pinnedCount_, items_.end(),
                   [](const Item& a, const Item& b) { return a.ts > b.ts; });
}

}  // namespace cv
