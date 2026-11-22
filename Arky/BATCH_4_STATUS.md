# Arky 0.4 — Batch 4 checkpoint

**Scope:** portable archive fidelity, safer extraction, progressive listing, and regression coverage. Built from the Batch 3 source checkpoint.

## Implemented

1. Progressive metadata listing via `ArchiveEngine::inspectBatched` with bounded 512-entry batches and `QAbstractListModel` append support; a `QSemaphore` caps pending GUI batches at four, and a final sort normalizes folder order.
2. `ReadLimits`/`ExtractOptions` for decoded-byte and entry-count caps plus approximate decompression expansion-ratio detection. Applied during extraction and integrity testing.
3. Preserved regular-file timestamps and basic permissions when creating archives; attempted safe restoration for extracted files/directories. Errors are surfaced as warnings rather than silently ignored.
4. Edit preflight blocks unsupported ACL/xattr/sparse/flags metadata and commented ZIP archives. Existing encrypted archive editing remains unsupported.
5. Destination guards reject pre-existing symlinks; on Linux use directory-relative no-follow output creation. Rollback removes newly created extraction outputs after failure or cancellation.
6. New tests for bounded listing, limit violations, format metadata, ZIP archive comments and Linux symlink destination rejection.

## Test evidence

GCC C++20 native archive and transfer-planning suites passed; both also passed ASan + UBSan. See `TEST_RESULTS.md`.

## Still outstanding (highest priority)

- **P0:** Compile and run on Windows using Qt SDK/MSVC (`scripts/verify-windows.ps1`). Verify QML startup, all Qt metaobject wiring, native Explorer drag/copy/paste, encrypted flows and UI editing commands. Linux core-test success is not Windows GUI verification.
- **P0:** Fuzz corrupted/untrusted archives, add hard resource budgets for codecs/processes, and test Windows reparse-point/race behavior; don't claim security completeness.
- **P1:** Add optional backend capabilities for multi-volume, alternate 7z/ZIP features, RAR creation where legally/technically supported, richer archive properties/comments and more compression controls.
- **P1:** Add large archive UX instrumentation, search/sort performance measurements, and memory tests with millions of metadata entries. Progressive batch display still retains full index eventually.
- **P1:** Preservation strategy for unsupported extended metadata and format-specific compression/encryption settings instead of silent re-encode; otherwise keep conservative rejection.
- **P2:** Signed packaging/installer, shell integration, macOS file promises and inbound Windows virtual-file transfers.

**Recovery:** This ZIP is a source checkpoint, not a Windows-tested binary release. Use backups when testing edit operations against important archives.
