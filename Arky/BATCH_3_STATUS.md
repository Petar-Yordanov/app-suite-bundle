# Arky 0.3 — Batch 3: native MIME, clipboard and file transfer

## Changes implemented in source

1. **Windows Shell virtual-file source (experimental)**: `WindowsVirtualMimeData` + `QWindowsMimeConverter` expose `FileGroupDescriptorW` (`TYMED_HGLOBAL`) and indexed `FileContents` (`TYMED_ISTREAM`). Each COM `IStream` lazily opens libarchive and decompresses in bounded 64-KiB requests. Supports read, rewind/seek, clone, stat and copy-to. No whole-file buffer and no disk pre-extraction are required for dragging **regular files** from Arky to Explorer. Mixed folders/non-regular entries use the staged-file fallback. Requires Qt 6.6+ and a Windows-specific build.
2. **Persistent local file exports**: replaced ephemeral `QTemporaryDir` with unique cache directories under `QStandardPaths::AppLocalDataLocation/exports`. Exports have a marker and are automatically pruned after 7 days upon application start (the current clipboard's referenced paths are excluded). Switching archives, changing selections or exiting the process no longer deletes pasted file paths immediately. The price is temporary *disk* usage; it is not a zero-space clipboard mode.
3. **Flat portable file URL layout**: staged selections are extracted to `source/` then moved to `ready/` with unique case-insensitive filenames. A selected parent directory subsumes its selected children. The staging `source/` and `ready/` directory names cannot conflict with archive contents.
4. **Windows explicit copy semantics**: for real local-file URL data, Qt's `QMimeData` additionally provides the Shell `Preferred DropEffect` set to `DROPEFFECT_COPY` so Explorer should copy staged files rather than move them. Only file paths that actually exist are advertised as file URLs.
5. **QML interaction**: supports initiating drag by mouse movement rather than only `onPressAndHold`. On macOS/Linux, selection of small known-size files triggers asynchronous bounded pre-staging. Larger files/folders can be explicitly prepared then dragged; Windows regular files use virtual drag as a one-gesture path.
6. **New tests**: `transfer_plan_tests.cpp` validates case-insensitive filename collisions, nested selections and unsafe paths on Linux. `mime_bridge_tests.cpp` now includes cache tests and Windows-only converter/COM streaming tests. CI and `scripts/verify-windows.ps1` run the new target.

## Verified locally

- `archive_tests`: PASS with GCC C++20 against installed libarchive 3.7.4.
- `transfer_plan_tests`: PASS with GCC C++20, also PASS with ASan+UBSan.
- Manual source checks for Xmake inputs/QML resources: performed.

## NOT verified — do not assume Windows interoperability works yet

- Qt GUI and `mime_tests` cannot be compiled or executed here because Qt 6 SDK is unavailable. QML syntax validation with the actual Qt parser is also pending.
- MSVC compilation of `windows_virtual_mime.cpp`, actual Explorer `FileGroupDescriptorW` + IStream drag, binary clipboard paste after exit and cross-app behavior on macOS/Linux need target-machine tests. The Windows converter is platform-specific source, not a production-tested feature.
- Inbound virtual-file transfers from Outlook and other `IDataObject` providers are not implemented.
- macOS file promises (`NSFilePromiseProvider`) and Windows Explorer virtual-file **clipboard** (as distinct from drag-out) are not implemented; copy/paste uses durable real file URLs.
- Export cache is retained for 7 days, so very large copied folders consume storage. There is no quota/size-management UI yet; old exports are pruned when the app next launches. For emergency cleanup use the `exports` folder under the application's local data directory, but never delete files still referenced by a clipboard/drag operation.
- Existing metadata fidelity, transactional editing safety against concurrent filesystem changes, extraction race mitigation and other Batch 4 gaps still exist.

## Windows acceptance checks

Run from **x64 Native Tools Command Prompt for VS 2022**:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1
```

Then manually test: (a) drag one and multiple regular files directly from the QML list to Explorer; (b) drag empty and nonempty folders via the disk-backed fallback; (c) copy from the archive, close Arky, paste into Explorer; (d) duplicate basenames from separate archive subfolders; (e) a large file without a large RAM spike; (f) encrypted archive entry, correct and incorrect password; (g) Unicode filenames and failure/cancellation paths; (h) importing Explorer files into an open archive.

If any Windows compile errors occur in the new Windows MIME code, treat that as priority zero before integrating more UI features.
