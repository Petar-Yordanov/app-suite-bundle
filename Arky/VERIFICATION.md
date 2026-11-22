# Arky 0.9.0 verification

## Executed in this development environment

- Standalone C++20 transfer-planning regression test: PASS.
- Headless CLI translation unit syntax check (GCC, C++20): PASS.
- QML resource/structure, controller bridge and moc source checker: PASS.

The full native build, libarchive/libzip engine tests, Qt MIME tests,
QML runtime smoke tests, deployment tests and comparative benchmarks were
not executed here: the environment lacks Xmake and Qt, libarchive and libzip
development SDKs. The combined workflow is configured for platform builds
and regression tests.

## Tests included

Engine tests cover supported writers, roundtrips, selection, traversal,
cancellation, conflicts, ZIP comment preservation, transactional edits,
read-only non-ZIP formats, numeric split reading and password extraction.
Transfer fixtures exercise Windows file descriptors and MIME behavior.

## Real desktop checks still required

- Open a large archive; navigate during enumeration.
- Edit a ZIP; check that other formats disable modification.
- Extract a protected ZIP with correct/wrong passwords and cancellation.
- Verify conflict choices, destination safety and source Trash policy.
- Open nested archives and individual files; check temporary-file lifetimes.
- Test file associations, drag/drop and portable settings on each OS.
- Check installers on fresh systems and collect benchmark results.
