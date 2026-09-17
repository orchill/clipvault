# ClipVault — Design Document

A native Windows clipboard history utility focused on near-zero idle resource usage.
Target: Windows 10 (1607+) / Windows 11. No runtime dependencies, no installer required.

---

## 1. Technology stack

| Layer          | Choice                              | Why |
|----------------|-------------------------------------|-----|
| Language       | C++20 (single executable, Win32 subsystem) | Native code; no VM/GC; smallest possible working set; deterministic cleanup via RAII. |
| UI framework   | **Raw Win32 + GDI+ custom painting** (no framework) | Any packaged UI stack (WinUI 3, WPF, Electron, Qt) drags in tens of MB of runtime RAM and a GPU-composited tree that repaints at idle. A hand-painted Win32 popup repaints **only when the user interacts** and costs ~0 otherwise. |
| Rendering      | GDI+ (shapes/images) + GDI `DrawTextW` (text, incl. color emoji via system font fallback) | GDI+ is in-box since XP, needs no extra DLL, supports anti-aliased rounded rects and thumbnails. GDI text gives ClearType + automatic font linking (color emoji works in plain `DrawTextW` on Win 8.1+). |
| Build          | CMake (primary) + `build.bat` (one-shot MSVC or MinGW) | Reproducible; builds with MSVC or MinGW-w64. |
| Libraries linked | `gdi32`, `gdiplus`, `user32`, `shell32`, `comctl32`, `dwmapi`, `uxtheme`, `advapi32` | All are inbox system DLLs. See README for why each exists. |

**No third-party dependencies.** Everything links against Windows system libraries.

## 2. Architecture overview

Ten small modules with one-way dependencies:

```
main.cpp ─► app (orchestrator: monitor, tray, hotkey, timers)
              ├─► capture      (extract formats from the clipboard)
              ├─► imaging      (worker thread: DIB→PNG/JPEG, thumbnails)
              ├─► history      (ordering, dedup, pin, limits, search view)
              ├─► storage      (JSON metadata + blob files, atomic writes)
              ├─► restore      (rebuild clipboard formats from an item)
              ├─► popup        (main history window, custom painted)
              ├─► settings_win (settings dialog window)
              └─► theme        (palette, fonts, DPI metrics)
settings ──► util (JSON, UTF-8/16, hashing, atomic file IO)
```

All state lives on the UI thread. Exactly **one** worker thread exists, used only for
image encode/thumbnail work that could take >50 ms (see §6).

## 3. Clipboard monitoring approach (event-driven, zero polling)

* `AddClipboardFormatListener(hwndMain)` → the OS posts `WM_CLIPBOARDUPDATE` **only when
  the clipboard content changes**. There is no polling loop, no timer, no background scan.
  Idle CPU is literally zero (the thread sits in `GetMessage`).
* While the app itself restores an item, a re-entrancy guard (`g_selfWrite` counter +
  timestamp/hash check) prevents capturing our own writes.
* **Pause monitoring** removes the listener entirely (`RemoveClipboardFormatListener`),
  so not even the message arrives.
* Persistence is debounced: changes set a 1.5 s `WM_TIMER`; only the timer tick writes to
  disk. Rapid copy bursts produce **one** disk write.

## 4. Clipboard format handling strategy

Priority when several formats are present (first match wins):

1. **Image** — `CF_DIBV5`/`CF_DIB`/`PNG` registered format → stored as PNG (or JPEG when
   opaque and PNG > 2 MB, for disk economy).
2. **RTF + text** — `Rich Text Format` stored raw; plain text kept for preview/search.
3. **HTML + text** — `HTML Format` stored raw.
4. **Plain/Unicode text** — `CF_UNICODETEXT`; UTF-16 preserved exactly (emoji, astral
   characters such as 𝓗𝓮𝓵𝓵𝓸, Ｆｕｌｌｗｉｄｔｈ, combining marks — nothing is normalized).
5. **Files** — `CF_HDROP` (file list).

Nothing is flattened to plain text: restore puts back **every captured format**
(e.g. RTF item → `CF_UNICODETEXT` + `Rich Text Format`; image → `CF_DIBV5` + `CF_DIB` +
`PNG`). Duplicates are detected by a 64-bit FNV-1a hash of the canonical content.

## 5. Memory optimization strategy

* Text: at most the first **64 K chars** stay in RAM (preview + search); the full text
  (capped at 4 MB) goes to a blob file and is loaded only on restore.
* Images: the raw DIB exists **transiently** during capture, is encoded to a file on the
  worker thread, and released. Only a **96 px thumbnail** (~37 KB) is kept per item.
* Thumbnails are generated once per item, off the UI thread, delivered via `PostMessage`.
* No global image cache; deleting an item frees its thumbnail immediately (RAII).
* Blob files are content-hashed → identical copies share one file; reference-counted GC
  deletes blobs when no item references them.
* History is bounded (`maxItems`, default **25** unpinned, plus pinned; image budget
  default 200 MB, pruned oldest-first).

## 6. Threading model

* **UI thread**: everything, including clipboard capture (memcpy-speed byte copies while
  the clipboard is open — closed immediately).
* **One worker thread** (`imaging`): PNG/JPEG encoding + thumbnail generation for image
  captures (a 4K screenshot encode can take 100–500 ms — too long for the UI thread).
  Job queue (`mutex` + `condition_variable` + `deque`), results posted back with
  `PostMessage`. No thread-per-operation, no polling.
* Image items appear in the list a few hundred ms after copying (after encode), text
  items are instant.

## 7. Persistence strategy

`%LOCALAPPDATA%\ClipVault\`:

```
config.json   settings (tiny, written on OK in the Settings window)
items.json    metadata for all items (text previews, hashes, paths, flags)
blobs\        content-addressed payloads: {hash}.png/.jpg/.rtf/.html/.txt
```

* Metadata (small JSON) is kept **separate** from binary blobs, per spec.
* Writes: debounced (1.5 s), atomic (`*.tmp` → `MoveFileEx(REPLACE_EXISTING|WRITE_THROUGH)`,
  `FlushFileBuffers` before rename).
* Persistence can be disabled entirely ("Save history between restarts") — items then
  live in RAM only and `items.json` is neither read nor written.
* Corruption handling: if `items.json` fails to parse it is renamed
  `items.json.corrupt.<tick>` and history starts empty. Blobs missing on disk are
  tolerated (item previews degrade gracefully).
* "Clear history on exit", "Clear all", "Clear unpinned", per-item delete, and
  day-based auto-cleanup are all supported.

## 8. UI framework choice & justification

A single borderless popup (like Win+V): DWM rounded corners, system dark/light theming,
per-monitor-v2 DPI awareness, custom-painted virtual list (only visible rows are drawn),
overlay scrollbar, type filter chips (All/Text/Images/Pinned), pinned section separator,
search box (native EDIT control → free IME/CJK support), keyboard model
(↑/↓/Enter/Del/Ctrl+P/Esc), row hover actions (pin/delete), context menu, tray icon.
**Why not WinUI/WPF/Electron:** see §1 — this keeps idle RAM at a few MB and idle GPU/CPU
at zero, which is the point of the app.

Global hotkey: `RegisterHotKey` (default **Ctrl+Shift+V**; fully configurable in Settings;
Win+V deliberately avoided — it is owned by the OS clipboard history and is unreliable to
intercept). Selecting an item writes the formats back to the clipboard, hides the popup,
restores focus to the previous window and (optionally) sends Ctrl+V.

## 9. Security / privacy

* **No network code at all** — the binary contains no WinHTTP/WinInet/socket usage.
  Everything is local; no telemetry, no accounts.
* Password-manager caveat, honestly: content itself cannot be classified as "sensitive"
  with any reliability. What *is* realistic: the clipboard **source process** is known at
  capture time (`GetClipboardOwner` → process name), so ClipVault supports an
  **application exclusion list** (e.g. add `KeePass.exe`, `1Password.exe`) — nothing copied
  from those apps is recorded. Excluded apps are also enforced before any disk write.
* Sensitivity controls: per-item delete, Clear unpinned, Clear all, Clear-on-exit,
  Pause monitoring, disable persistence.

## 10. Windows-specific limitations (documented, by design)

* Apps using **delayed rendering** can make `GetClipboardData` block briefly while they
  render; a hung clipboard owner can block `OpenClipboard` — handled with a bounded
  retry loop, never crashes, and failure simply skips the item.
* **Win+V** is owned by the OS clipboard history; registering it only works when that
  feature is disabled, so the default hotkey is Ctrl+Shift+V (configurable).
* Windows does not deliver clipboard-change notifications with the source window for
  *some* apps (owner is NULL) — the source app shows as empty then.
* Image restore targets 32-bit BGRA DIBs; extremely old 16-color clipboard payloads are
  normalized through GDI at capture time.
* Elevated (admin) apps' clipboards are captured fine, but ClipVault cannot send Ctrl+V
  into an elevated window when running unelevated (UIPI) — the content is still placed
  on the clipboard for manual paste.

## Project layout

```
CMakeLists.txt  build.bat  README.md  DESIGN.md
resources/  app.rc  app.manifest  app.ico (generated by tools/gen_icon)
tools/      gen_icon.cpp (ICO generator, no external deps)
src/        main.cpp app.{h,cpp} capture.{h,cpp} restore.{h,cpp} imaging.{h,cpp}
            history.{h,cpp} storage.{h,cpp} settings.{h,cpp} theme.{h,cpp}
            util.{h,cpp} popup.{h,cpp} settings_window.{h,cpp}
```
