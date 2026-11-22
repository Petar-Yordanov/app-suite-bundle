# Netflix, Discord and website compatibility

## Start with the built-in diagnostic page

From the WebBrowser project directory after building:

```sh
xmake run webbrowser --compatibility-test
```

The same flags work when running the installed executable (on macOS use the executable inside the .app bundle). This mode starts a temporary profile and a read-only loopback webserver. It doesn't use your existing website logins. The page provides:

- H.264, AAC, VP9, AV1, Opus playback capability queries and H.264/AAC Media Source checks.
- Widevine EME negotiation and MediaKeys construction for WebM and MP4; these do **not** request a streaming-provider license or play encrypted content.
- WebRTC API, advertised RTP codecs, encoded-frame transform API (necessary but not sufficient for Discord's encrypted calls), and a real in-page peer-to-peer data exchange.
- An opt-in synthetic call that sends generated audio and animated video between two WebRTC peers, checking received RTP packets and decoded video frames.
- Explicit microphone/camera permission and physical device tests (local preview and microphone signal level, no recording).
- Local audio/video file playback tests; a file is not uploaded.
- Copyable JSON diagnostic report.

For automated desktop testing:

```sh
xmake run webbrowser --compatibility-smoke
xmake run webbrowser --compatibility-smoke --require-codecs
xmake run webbrowser --compatibility-smoke --require-widevine
```

The first command requires secure-origin media APIs, WebRTC, an encoded-transform API, and successful local WebRTC data exchange. The second also requires H.264/AAC playback and adaptive-streaming capabilities. The third additionally requires successful Widevine MP4 CDM initialization. A failed gate returns a nonzero exit code. Physical device tests and the synthetic media call require clicking the corresponding buttons; no camera or microphone is requested by CI. The standard CI installer build runs the first mode against every desktop package. Missing proprietary codecs or CDM are reported but do not fail this standard gate.

## 1. Widevine for desktop Netflix

First run the diagnostic page with no special flags. Qt WebEngine searches known Widevine locations, including supported Google Chrome installations. Chrome must have actually downloaded the CDM component. Its location and architecture depend on the Chrome installation and component version.

If the Widevine checks fail, find the trusted local CDM file in your Chrome installation or another properly installed/licensed Widevine distribution. It must match the operating system, architecture and Chromium CDM interface. Typical library filenames:

| Platform | Library file |
|---|---|
| Windows | widevinecdm.dll |
| Linux | libwidevinecdm.so |
| macOS | libwidevinecdm.dylib |

Use the explicit startup option, e.g. on Windows:

```powershell
xmake run webbrowser --compatibility-test --widevine-path "C:\path\to\widevinecdm.dll"
xmake run webbrowser --widevine-path "C:\path\to\widevinecdm.dll"
```

Linux/macOS: substitute the corresponding absolute .so/.dylib path. The application validates the path and configures Qt's documented Chromium flag before initialization. The path is not automatically copied, downloaded or persisted. Alternatively set `QTWEBENGINE_CHROMIUM_FLAGS` with Qt's documented `--widevine-path` flag, but do not combine that with the app's explicit option. Chrome's permissions and other security settings are not modified.

Only load a trustworthy CDM. If Google Chrome's CDM cannot be used on your engine version/platform, a properly licensed version is required. Widevine redistribution is subject to Google's terms; it is not included in this project's installers.

### 2. Enable H.264 and AAC codecs in the Qt SDK

Qt WebEngine's decoder is part of its Chromium build, not a Qt Multimedia or libVLC decoder. Installing FFmpeg or libVLC beside the application does not enable missing browser codecs.

If the diagnostics show H.264/AAC unavailable, build a matching Qt WebEngine module with proprietary codecs enabled. This can be time-consuming; Chromium's build is much larger than the browser shell. Keep a separate Qt installation for the codec-enabled build so the other apps and the stock browser SDK remain untouched.

1. Install a **fresh separate desktop Qt 6.11.3 kit** with Qt Base, Declarative, WebChannel, Positioning and WebEngine dependencies using the Qt installer. Acquire the corresponding complete Qt WebEngine source tree from the matching Qt source release (including Chromium sources). A shallow QtWebEngine Git checkout without its Chromium submodule/resources is not enough.
2. Install the source-build prerequisites for that Qt release: supported C++ toolchain, CMake/Ninja, Python with html5lib, Node.js, and platform-specific Chromium dependencies. On Windows use a native Visual Studio developer command prompt/PowerShell, not MSYS/MinGW. On macOS use the appropriate Xcode and matching CPU architecture. See the platform notes below.
3. Configure a fresh build directory outside the source tree. From that directory use the separate kit's `qt-configure-module`:

Linux/macOS:

```sh
export CODEC_QT="/absolute/path/to/separate/Qt/6.11.3/kit"
export QTWEBENGINE_SRC="/absolute/path/to/qtwebengine"
mkdir -p "$PWD/qtwebengine-codecs-build"
cd "$PWD/qtwebengine-codecs-build"
"$CODEC_QT/bin/qt-configure-module" "$QTWEBENGINE_SRC" -webengine-proprietary-codecs
cmake --build . --parallel
cmake --install .
```

Windows PowerShell:

```powershell
$CodecQt = 'C:\Qt-codecs\6.11.3\msvc2022_64'
$Source = 'C:\Qt\6.11.3\Src\qtwebengine'
New-Item -ItemType Directory -Force qtwebengine-codecs-build
Set-Location qtwebengine-codecs-build
& "$CodecQt\bin\qt-configure-module.bat" $Source -webengine-proprietary-codecs
cmake --build . --parallel
cmake --install .
```

Check the configure output for enabled proprietary codecs and the install destination (it must be the dedicated kit). Use a matching compiler, kit architecture, Qt version and build mode. These are Qt's module-build commands, not a replacement for the application Xmake build.

4. Return to `WebBrowser/`, reconfigure Xmake with the dedicated kit and rebuild:

```sh
xmake f -c -y -m release --qt="/absolute/path/to/separate/Qt/6.11.3/kit"
xmake build -y webbrowser
xmake run webbrowser --compatibility-smoke --require-codecs
xmake run webbrowser --compatibility-test
```

Use the architecture/platform options in README.md (Windows x64/MSVC, macOS arm64, Linux x86_64). Verify that the running app resolves its Qt libraries from the codec kit rather than a stock installation. Even if the codec probe passes, verify actual local H.264/AAC MP4 playback.

You can optionally set `WEBBROWSER_REQUIRE_CODECS=1` for the existing packaging scripts to enforce codecs in a deployed package. This does not automatically add codecs to Qt or authorize distribution. Only package/redistribute a codec-enabled build if all applicable licenses permit that. Normal CI stays on the stock SDK; no CDM is uploaded.

## 3. Test Netflix itself

After codec and Widevine gates pass, perform an actual DRM playback check. Use a legitimate public Widevine demo such as Shaka Player's demo (https://shaka-player-demo.appspot.com/demo/) and choose a Widevine-encrypted test asset. Playback, rather than just a successful EME query, is required. Then restart the browser normally, go to https://www.netflix.com, sign in to your own account and play an actual title. Try seeking, fullscreen, audio and subtitles; note any service error code and achieved resolution. No credentials are necessary for the diagnostic page.

Netflix's supported-browser list does not include Qt WebEngine. Therefore passing the local EME/codec checks doesn't guarantee Netflix accepts it or any particular quality (HD/4K). There is no DRM bypass or modified browser identity. If Netflix refuses playback despite a compatible interface, we cannot promise a fix using this engine.

On iOS, WKWebView relies on system WebKit and its DRM implementation; desktop Widevine DLLs cannot be loaded. The desktop `--require-widevine` test is not an iOS acceptance requirement. Netflix's native iPhone app is the fallback if the website doesn't support playback in an embedded WebKit view.

## 4. Test Discord itself

The application implements origin-specific camera/microphone permission handling and desktop capture-source selection. The site's web client handles Discord's voice protocol, including DAVE end-to-end encryption. The local diagnostics test some prerequisites but do not establish a real Discord call.

1. Run the diagnostic page and verify the WebRTC peer and encoded-transform tests, synthetic call, and real microphone/camera.
2. Open https://discord.com/app in a normal browser session. Sign in with your own account.
3. Enter a voice channel with another participant and verify both directions of audio. Check the encrypted-call indicator; run Discord's own mic test.
4. Test a video call, camera switching, mute/unmute, hangup/rejoin and background audio after switching tabs.
5. Test screen sharing, source selection, system prompts and stopping share. Audio sharing depends on OS/engine support.
6. Retry with the ad blocker off or Discord allowlisted if a third-party host was blocked. Do not bypass permissions or certificate checks.

Discord does not officially support mobile browsers. Native WebKit media permissions on iOS do not imply that Discord's mobile web client accepts voice/video.

## 5. Test the remaining sites

Use a normal browser session, not the temporary diagnostic profile. Repeat for Windows x64, Linux x64 and macOS arm64; separately test a signed iOS build where the site supports it.

| Site | Acceptance checks |
|---|---|
| YouTube | Play a video, choose resolution, seek, fullscreen, sound, background tab, sign-in if desired |
| YouTube Music | Song playback, pause/skip, background audio, login persistence |
| Microsoft 365 | Sign in, MFA, popup/redirect authentication, open and edit a document, save, reload |
| Discord | Login, chat, E2EE voice/video, device selection, screen sharing |
| Netflix | Authorized DRM playback, seek, subtitles/audio, fullscreen, achieved resolution |

Record pass/fail per OS and actual error messages. A real site may require browser features or identity/DRM policies not covered by local tests. Don't treat the local report as proof of service compatibility.

## Primary documentation

- https://doc.qt.io/qt-6.11/qtwebengine-features.html
- https://doc.qt.io/qt-6.11/qtwebengine-platform-notes.html
- https://developers.google.com/widevine/access
- https://help.netflix.com/en/node/30081
- https://support.discord.com/hc/en-us/articles/38025123604631-Minimum-Client-Version-Requirements-for-Voice-Chat
- https://support.discord.com/hc/en-us/articles/213491697-What-are-the-OS-system-requirements-for-Discord
