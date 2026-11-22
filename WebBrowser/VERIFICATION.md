# Verification status

Locally executed on Linux with GCC C++20:

- Pure C++ rule/filename unit tests: PASS.
- Rule microbenchmark: PASS; 20,016 compiled host rules, 200,000 checks, 100,000 matches. Example run: ~13.6 ms compile, ~64 ms checks. These timings are specific to the current machine, not browser performance.
- Requirement audit regression run: rule tests PASS; ~11.75 ms compile, ~67.65 ms checks. Python syntax, QRC and plist parsing, workflow YAML and desktop matrix checks, release manifest generation and SHA-256 verification: PASS.
- Integrated workflow parsed as YAML; packaging shell syntax checked.
- Compatibility diagnostic page JavaScript, Python harness and QRC syntax checks: PASS.
- Chromium/Playwright diagnostic execution attempted; Chromium could not start because the workspace prohibits creation of its process-singleton socket. No WebRTC, CDM or physical media runtime result is claimed.

The desktop installer scripts now run `--compatibility-smoke` against the deployed browser, in addition to the existing browser integration smoke test. Proprietary codec and Widevine requirements are opt-in gates. See COMPATIBILITY.md for manual local media and live-site acceptance checks and the separate Qt codec build.

The updated integration smoke verifies normal profile cookies, isolation of private cookies, disposal of the last private session, and exclusion of private visits. Store tests now cover independent session selection restoration, late title changes, SPA navigation, and blocking unsafe address schemes. These Qt-dependent tests are wired into CI but have not yet been run locally. See REQUIREMENTS.md for the full source audit.

The local environment lacks the required Qt 6.11 WebEngine SDK, Xmake, Apple Xcode and Windows toolchain. The complete native GUI, SQLite tests, Objective-C++ code, installers, live sites and browser performance comparison have **not** been executed here. They are included for CI/local validation, not represented as passing.

Acceptance checks on each desktop:

1. Launch installed package, navigate HTTPS, back/forward/reload, close/reopen/reorder tabs.
2. Restore several regular tabs and history after restarting; make sure the inactive tabs remain lazy. Try independently disabling each restore setting.
3. Check persistent login with a real service and separately toggle session-cookie restoration. Verify private isolation and closing the last private tab.
4. Test bookmark nesting/move/import/export, searching and clearing history.
5. Download a large resumable file; pause, resume, cancel, completed path, restart without private history.
6. Test media permissions on Discord and an origin with microphone/camera access. Denials must remain denied for that request; never bypass OS permissions.
7. Test Office 365 sign-in, document editing, YouTube, YouTube Music; verify actual audio/video codecs. Netflix requires a legitimate CDM and compatible platform; record actual behavior.
8. Compare process tree memory and CPU via the harness under identical conditions.
9. iOS simulator: compile, open local fixture, verify touch/layout and tabs. On signed iPhone: verify web permissions, login/popups, downloads in Files, rotation and private browsing.
