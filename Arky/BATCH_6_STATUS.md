# Arky 0.6 — Batch 6 status (2026-11-13)

## Implemented

1. FileGroupDescriptorW drag-out now supports selected folder trees, including empty folders, with stable descriptor ordering and matching FileContents stream indices for files only.
2. Streaming stays bounded to 64 KiB per COM IStream read; no entire archive members are placed into QMimeData.
3. Added Windows-specific validation of transfer names, reserved DOS devices, invalid characters, path collisions, and descriptor count. Large folder trees use disk-backed staging rather than rescanning the archive once per member.
4. New Windows-only **Copy virtual** UI action for live Shell descriptors. **Copy files** stays persistent, disk-backed and interoperable with ordinary `text/uri-list`/CF_HDROP paths.
5. Exposes file modification timestamps and `Preferred DropEffect=COPY` in Shell formats, while retaining custom Arky JSON metadata.
6. Updated Windows native MIME tests for directory descriptors, nested streams, indices, and copy preference; expanded portable tests for malformed filenames and collisions.

## Verified

- Linux libarchive engine regression suite and portable transfer planner: PASS (normal + ASan/UBSan).
- QML/C++ source consistency check: PASS.

## Not yet verified or implemented

- **Windows MSVC + Qt GUI build and Explorer interoperability have not been exercised** in this Linux environment (no Windows SDK or Qt installed).
- Incoming virtual attachments from Outlook and other IDataObject sources remain unsupported; the app can paste standard actual file URLs.
- Mac file promises/Wayland interprocess drag specifics remain unsupported.
- Windows native virtual clipboard requires the app to remain open and the archive to remain available and unchanged until paste completes.
- Very large trees use disk-backed staging, which can consume disk space; archive read complexity for small native transfers is still sequential for each entry.
- Platform-specific Unicode filesystem collision behavior and real Windows shell handling still need automated end-to-end verification.

## Next technical work

- Windows CI compilation and runtime tests should block releases (the workflow is supplied, but no CI result exists yet).
- Add receiving native virtual attachments via bounded, isolated on-disk streaming; support macOS file promises.
- Add format/backend capability negotiation, enhanced integrity properties and extraction edge-case fuzzing.

## User action

Only build and launch on Windows:

```bat
xmake f -p windows -a x64 -m release -y
xmake -y
xmake run Arky
```

No manual MIME test scenarios are being requested from the user. The automated Windows verifier is in `scripts/verify-windows.ps1` and `.github/workflows/windows-qt-smoke.yml`.
