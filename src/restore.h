// restore.h — write an item's formats back to the Windows clipboard.
#pragma once
#include "history.h"

namespace cv {

// Writes all captured formats (text+RTF / text+HTML / DIBV5+DIB+PNG / HDROP).
// Returns false when the clipboard is locked or the payload is unavailable.
bool RestoreItemToClipboard(const Item& it);

}  // namespace cv
