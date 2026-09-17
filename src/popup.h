// popup.h — the main history popup window (custom painted Win32).
#pragma once
#include "app.h"

namespace cv {

bool CreatePopupWindow(HINSTANCE hInst);
void PopupReset();    // fresh open: clear search/filter/selection
void PopupRefresh();  // contents changed: rebuild view (+ repaint when visible)
void PopupRebuild();  // metrics/theme changed: re-layout + repaint
void PopupActivateSelection();
void PopupDeleteSelection();
void PopupPinSelection();
void PopupCopySelection();
void PopupMoveSelection(KeyCmd cmd);

}  // namespace cv
