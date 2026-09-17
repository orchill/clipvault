// storage.h — load/save history metadata (items.json) with atomic, debounced writes.
#pragma once
#include "history.h"

namespace cv {

class Storage {
 public:
  // Loads items.json (if persist enabled). Corrupt files are quarantined and
  // history starts empty. Missing blobs are tolerated.
  void Load(History& hist);
  // Serializes and atomically writes items.json (only when persist enabled).
  bool Save(History& hist);
};

}  // namespace cv
