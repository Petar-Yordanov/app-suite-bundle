# Arky 0.7 — Batch 7 checkpoint (2026-11-13)

## Scope completed

1. Added a `FormatCapabilities` model and `ArchiveEngine::capabilities()` which reports supported write formats, whether existing archives can be edited, standalone vs multi-file containers, compression levels, encryption modes, and lack of multi-volume support.
2. Added writer-side runtime detection of supported ZIP/7z compression methods and ZIP AES modes. Absent optional compression libraries no longer produce misleading UI options.
3. Added `CreateOptions::compressionMethod` and `CreateOptions::encryption`, both checked against the selected format before creating output. Supported ZIP methods include store/deflate and optional codecs; 7z includes store and LZMA variants plus available optional codecs.
4. Implemented ZIP AES-128 and AES-256 creation with passphrases; automatic password-protected ZIP creation remains AES-256. 7z encryption is explicitly rejected rather than creating a misleading unencrypted archive.
5. Exposed dynamic format options to QML with a scrollable sidebar, selected save-dialog format, and mismatch validation. No change to the bounded libarchive file-content streaming architecture.
6. Added codec tests and real ZIP local-header checks, encrypted round-trip tests, option-negative tests and source-level consistency validation.

## Verified

- Linux GCC core regression and transfer-planning tests passed.
- The new optional-codec cases pass when the writer accepts the method; unsupported optional codecs on system libarchive 3.7.4 are capability-probed and skipped.
- ASan/UBSan runs passed for archive and transfer tests.
- QML resource + controller declaration consistency checker passed.

## Not verified

- Qt 6/QML runtime and Windows MSVC compilation are NOT available in this environment.
- Windows Explorer native MIME behavior, macOS file promises and Shell clipboard scenarios remain unverified.
- Qt Quick sidebar layout has not been visually inspected at runtime.

## Gaps and next priorities

- **Alternate format backend** (e.g. optional 7-Zip executable integration) for encrypted 7z, 7z header encryption, RAR creation where legally available, and split/multi-volume archives. Such integrations should use explicit capability negotiation and bounded temp staging.
- ZIP metadata, archive comments, compressed-size and per-entry properties/integrity details; don't silently degrade existing metadata.
- Runtime CI verification must pass on Windows before treating the project as a daily-use release.
- Security/fuzzing and performance checks on hostile/huge real-world archives still need a full pass.

## Windows build/run (your only manual action)

```bat
xmake f -p windows -a x64 -m release -y
xmake -y
xmake run Arky
```

`scripts/verify-windows.ps1` and `.github/workflows/windows-qt-smoke.yml` remain supplied for automated tests. No Windows build result has been claimed.
