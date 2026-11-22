# Arky 0.5 — Batch 5 status (2026-11-13)

**Scope:** Stabilization and automated verification infrastructure for Windows Qt/QML builds. Based on 0.4 Batch 4. Source checkpoint, not a verified Windows binary.

## Completed

1. Fixed the smoke-test race caused by `archiveChanged` being emitted at the start of an async archive load, before the index was populated.
2. Replaced the single-assertion Qt smoke check with an end-to-end test of Qt Quick loading + backend ZIP open, integrity test, extraction with byte comparison, rename, disk-backed clipboard MIME file URLs, remove, and empty-folder creation. Exit codes identify failed stages.
3. Moved Qt Quick Controls Basic-style selection to a `.conf` file compiled into Qt resources, avoiding an unnecessary C++ QuickControls2 linkage requirement.
4. Pinned Xmake's Qt Quick package to 6.9.1, matching CI's Qt 6.9.1 MSVC2022 SDK.
5. Updated Windows GitHub Actions to provision the SDK (rather than assume one exists) and execute source checks and the existing Windows verification script.
6. Added `scripts/source-check.py` to catch missing Qt resources, Qt moc header mistakes and QML/controller API mismatches before attempting builds.
7. Refreshed setup/build verification documentation.

## Verified in this environment

- PASS: portable GCC C++20 archive core suite, normal build.
- PASS: portable GCC C++20 transfer planning suite, normal build.
- PASS: both suites built and passed with AddressSanitizer + UndefinedBehaviorSanitizer and leak checking.
- PASS: source checker (`scripts/source-check.py`).

## Unverified / blocked

- **No Qt development SDK or MSVC in this Linux container.** Cannot compile or execute Qt-specific `mime_tests` or `--smoke-test` or evaluate GitHub Actions results.
- **Windows Explorer drag/drop** and `FileGroupDescriptorW` streaming still require live Windows interoperability tests.
- CI job is authored but **has not run**; a passing GitHub Actions badge is NOT implied.
- QML and C++ source changes cannot be considered compiler-verified until the Qt Windows job actually completes.

## Next work

- Batch 6: Windows native clipboard/Explorer integration tests; inbound virtual file handling and robust drag/drop across file and directory selections.
- Batch 7: advanced format backends/option negotiation, multi-volume behavior, integrity and properties UI.
- Release hardening: run the CI, fix any MSVC or Qt runtime errors it finds, fuzz unsafe archive inputs, verify Windows reparse point defense, then add signed and reproducible packaging.

## Minimal Windows use

From x64 Native Tools Command Prompt in the extracted project root:

```cmd
xmake f -p windows -a x64 -m release -y
xmake -y
xmake run Arky
```

Qt 6.9.1 can be provided explicitly with `xmake f ... --qt=C:\Qt\6.9.1\msvc2022_64` if Xmake cannot resolve the declared SDK automatically. The automated verification command (optional) is `powershell -NoProfile -ExecutionPolicy Bypass -File scripts\verify-windows.ps1`.
