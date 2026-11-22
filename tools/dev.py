#!/usr/bin/env python3
"""Configure/run an App Suite target using a discovered or cached Qt SDK."""
import argparse
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APPS = {
    "Arky": "Arky", "ClipboardManager": "clipdeck", "DockerGUI": "dockergui",
    "ImageViewer": "imageviewer", "MediaPlayer": "mediaplayer",
    "ScreenRecorder": "screenrecorder", "ScreenshotTool": "screenshottool",
    "SystemMonitor": "systemmonitor", "TerminalEmulator": "terminalemulator",
    "WebBrowser": "webbrowser",
}
HOST = platform.system()
CACHE = Path.home() / ".appsuite"


def run(args, **kwargs):
    print("+", subprocess.list2cmdline([str(a) for a in args]), flush=True)
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def host_config():
    arch = platform.machine().lower()
    arm = arch in {"arm64", "aarch64"}
    if HOST == "Windows" and not arm:
        return "windows", "x64", "windows", "win64_msvc2022_64", ("msvc2022_64",)
    if HOST == "Darwin":
        return "macosx", "arm64" if arm else "x86_64", "mac", "clang_64", ("macos", "clang_64")
    if HOST == "Linux":
        return "linux", "arm64" if arm else "x86_64", "linux", "linux_gcc_arm64" if arm else "linux_gcc_64", ("gcc_arm64", "linux_gcc_arm64") if arm else ("gcc_64", "linux_gcc_64")
    raise RuntimeError("No automatic native Qt kit for this platform. Use a supported host/SDK.")


def query_qt(candidate):
    for qmake in (candidate / "bin" / "qmake.exe", candidate / "bin" / "qmake6", candidate / "bin" / "qmake"):
        if not qmake.is_file():
            continue
        try:
            result = subprocess.run([str(qmake), "-query"], check=True, capture_output=True, text=True, timeout=15)
            data = dict(line.split(":", 1) for line in result.stdout.splitlines() if ":" in line)
            if data.get("QT_VERSION", "").startswith("6."):
                return data
        except (OSError, subprocess.SubprocessError):
            pass
    return None


def compatible(candidate, app, minimum):
    data = query_qt(candidate)
    if not data:
        return False
    try:
        version = tuple(map(int, data["QT_VERSION"].split(".")))
        if version < tuple(map(int, minimum.split("."))):
            return False
        lib = Path(data["QT_INSTALL_LIBS"])
        plugins = Path(data["QT_INSTALL_PLUGINS"])
        qml = Path(data["QT_INSTALL_QML"])
    except (KeyError, ValueError):
        return False
    modules = ["Core", "Gui", "Widgets", "Qml", "Quick", "QuickControls2", "Network", "Concurrent"]
    if app == "ImageViewer":
        modules.append("Svg")
        if not all(any((plugins / "imageformats").glob(f"*{name}*")) for name in ("qwebp", "qtiff")):
            return False
    if app == "MediaPlayer":
        modules.append("QuickWidgets")
    if app in {"ScreenshotTool", "ScreenRecorder"} and HOST == "Linux":
        modules.append("DBus")
    if app == "WebBrowser":
        modules.extend(["Sql", "WebEngineCore", "WebEngineQuick", "WebChannel", "Positioning"])
        if not any((plugins / "sqldrivers").glob("*qsqlite*")):
            return False
    if HOST == "Windows" and "msvc" not in data.get("QMAKE_XSPEC", "").lower():
        return False
    return all((lib / "cmake" / f"Qt6{module}" / f"Qt6{module}Config.cmake").is_file() for module in modules) and (qml / "QtQuick" / "Controls").is_dir()


def discover_qt(app, minimum, kits, explicit=None):
    if explicit:
        candidate = explicit.expanduser().resolve()
        if not compatible(candidate, app, minimum):
            raise RuntimeError(f"Incompatible or incomplete Qt SDK: {candidate} (need Qt {minimum}+ and required modules)")
        return candidate
    candidates = []
    if os.environ.get("QT_ROOT_DIR"):
        candidates.append(Path(os.environ["QT_ROOT_DIR"]))
    roots = [CACHE / "Qt", Path.home() / "Qt", Path("C:/Qt") if HOST == "Windows" else Path("/opt/Qt")]
    if HOST == "Darwin":
        roots.extend([Path("/opt/homebrew/opt/qt"), Path("/usr/local/opt/qt")])
    for root in roots:
        candidates.append(root)
        candidates.extend(root / minimum / kit for kit in kits)
        for version_dir in sorted(root.glob("6.*"), reverse=True):
            candidates.extend(version_dir / kit for kit in kits)
    for name in ("qmake6", "qmake"):
        exe = shutil.which(name)
        if exe:
            candidates.append(Path(exe).resolve().parent.parent)
    for candidate in dict.fromkeys(candidates):
        if compatible(candidate, app, minimum):
            return candidate.resolve()
    return None


def acquire_qt(app, minimum, host, arch, kits, allow_download):
    if not allow_download:
        raise RuntimeError(f"No compatible Qt {minimum}+ SDK found for {app}; install one or omit --no-download.")
    # aqt 3.3.0 cannot read the per-architecture Windows Qt 6.11 repository.
    # Use the same pinned upstream fix as CI, even if an old aqt is on PATH.
    browser_windows = app == "WebBrowser" and host == "windows"
    if shutil.which("aqt") and not browser_windows:
        aqt = [shutil.which("aqt")]
    else:
        venv = CACHE / ("aqt-qt611-venv" if browser_windows else "aqt-venv")
        python = venv / ("Scripts/python.exe" if HOST == "Windows" else "bin/python")
        if not python.is_file():
            run([sys.executable, "-m", "venv", venv])
        if browser_windows:
            run([python, "-m", "pip", "install",
                 "git+https://github.com/miurahr/aqtinstall.git@076e1659807d0b362a3ed684d54c2e9c775eb9c7",
                 "py7zr==1.1.4"])
        else:
            run([python, "-m", "pip", "install", "aqtinstall"])
        aqt = [python, "-m", "aqt"]
    qt_root = CACHE / "Qt"
    qt_root.mkdir(parents=True, exist_ok=True)
    # The regular SDK also includes the additional image plugins; share it
    # between all nine ordinary desktop projects. WebEngine stays separate.
    modules = ["qtimageformats"] if app != "WebBrowser" else ["qtwebengine", "qtwebchannel", "qtpositioning", "qtserialport"]
    run([*aqt, "install-qt", host, "desktop", minimum, arch, "--outputdir", qt_root, "-m", *modules])
    result = discover_qt(app, minimum, kits)
    if result is None:
        raise RuntimeError("Qt was downloaded but failed SDK/module checks. Check native prerequisites and architecture.")
    return result


def prepare_vlc(env, allow_download):
    project = ROOT / "MediaPlayer"
    cache = project / ".deps" / "vlc"
    headers = [Path(env["VLC_INCLUDE_DIR"])] if env.get("VLC_INCLUDE_DIR") else []
    headers.extend([cache / "vlc-3.0.24" / "include", Path("/usr/include"), Path("/usr/local/include"), Path("/opt/homebrew/include")])
    include = next((p for p in headers if (p / "vlc" / "vlc.h").is_file()), None)
    runtimes = [Path(env["VLC_SDK_ROOT"])] if env.get("VLC_SDK_ROOT") else []
    if HOST == "Windows":
        runtimes.extend(p.parent for p in (cache / "runtime").rglob("libvlc.dll"))
        runtimes.append(Path(env.get("ProgramFiles", "C:/Program Files")) / "VideoLAN" / "VLC")
        valid_runtime = lambda p: (p / "libvlc.dll").is_file() and (p / "plugins").is_dir()
    elif HOST == "Darwin":
        runtimes.extend([cache / "VLC.app" / "Contents" / "MacOS", Path("/Applications/VLC.app/Contents/MacOS")])
        valid_runtime = lambda p: (p / "lib" / "libvlc.dylib").is_file() or (p / "lib" / "libvlc.5.dylib").is_file()
    else:
        # Linux loads libvlc.so.5 through the native dynamic loader.
        if include is None:
            raise RuntimeError("Install libvlc-dev and the VLC 3 runtime/plugins using your distro package manager.")
        return [f"--vlc_include={include}"]
    runtime = next((p for p in runtimes if valid_runtime(p)), None)
    if include is None or runtime is None:
        if not allow_download:
            raise RuntimeError("libVLC 3 SDK/runtime not found; set VLC_INCLUDE_DIR and VLC_SDK_ROOT or enable downloads.")
        if HOST == "Windows":
            shell = shutil.which("pwsh") or shutil.which("powershell.exe")
            if not shell:
                raise RuntimeError("PowerShell is required to fetch the official Windows VLC runtime.")
            run([shell, "-NoProfile", "-File", "Packaging/fetch-vlc-windows.ps1"], cwd=project, env=env)
        else:
            run(["bash", "Packaging/fetch-vlc-macos.sh"], cwd=project, env=env)
        return prepare_vlc(env, False)
    env.update(VLC_INCLUDE_DIR=str(include.resolve()), VLC_SDK_ROOT=str(runtime.resolve()))
    if HOST == "Windows":
        env["PATH"] = str(runtime.resolve()) + os.pathsep + env.get("PATH", "")
    return [f"--vlc_include={include.resolve()}", f"--vlc_root={runtime.resolve()}"]


def check_native(app):
    if app == "ScreenRecorder" and not shutil.which("ffmpeg"):
        locations = [Path("/opt/homebrew/bin/ffmpeg"), Path("/usr/local/bin/ffmpeg")]
        if not any(p.is_file() for p in locations) and not os.environ.get("FFMPEG_BINARY"):
            print("WARNING: install FFmpeg before recording (the application requires its executable at runtime).", file=sys.stderr)
    if HOST == "Linux" and app in {"ClipboardManager", "ScreenshotTool", "ScreenRecorder", "MediaPlayer"}:
        packages = ["x11"]
        if app in {"ScreenshotTool", "ScreenRecorder"}:
            packages += ["xrandr", "xfixes"]
        if app == "ScreenRecorder":
            packages += ["gstreamer-app-1.0", "gstreamer-video-1.0"]
        pkgconfig = shutil.which("pkg-config")
        if not pkgconfig or subprocess.run([pkgconfig, "--exists", *packages]).returncode:
            raise RuntimeError(f"Missing Linux development packages: {', '.join(packages)}. See LOCAL-BUILD.md.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", choices=APPS)
    parser.add_argument("--qt", type=Path, help="Explicit SDK directory; no download if this path is invalid")
    parser.add_argument("--mode", choices=["debug", "release"], default="release")
    parser.add_argument("--no-download", action="store_true", help="Do not download Qt or libVLC SDKs (Xmake may still acquire project packages)")
    parser.add_argument("--configure-only", action="store_true")
    parser.add_argument("--build-only", action="store_true")
    args, app_args = parser.parse_known_args()
    if app_args and app_args[0] != "--":
        parser.error("Pass application arguments after --")
    if args.configure_only and args.build_only:
        parser.error("Choose only one of --configure-only and --build-only")
    xmake = shutil.which("xmake")
    if not xmake:
        raise RuntimeError("Install Xmake first (https://xmake.io).")
    plat, arch, host, qt_arch, kits = host_config()
    check_native(args.app)
    minimum = "6.11.3" if args.app == "WebBrowser" else "6.8.3"
    qt = discover_qt(args.app, minimum, kits, args.qt)
    if qt is None:
        qt = acquire_qt(args.app, minimum, host, qt_arch, kits, not args.no_download)
    print(f"Using Qt SDK: {qt}", flush=True)
    env = os.environ.copy()
    env["QT_ROOT_DIR"] = str(qt)
    for key in ("QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH", "QTWEBENGINEPROCESS_PATH"):
        env.pop(key, None)
    config = [xmake, "f", "-y", "-p", plat, "-a", arch, "-m", args.mode, f"--qt={qt}"]
    if HOST == "Windows":
        config += ["--vs=2022"]
    if args.app == "MediaPlayer":
        config += prepare_vlc(env, not args.no_download)
    if args.app == "ClipboardManager":
        config += ["--desktop=y"]
    project = ROOT / args.app
    run(config, cwd=project, env=env)
    if args.configure_only:
        return
    run([xmake, "build", "-y", APPS[args.app]], cwd=project, env=env)
    if not args.build_only:
        run([xmake, "run", APPS[args.app], *app_args[1:]], cwd=project, env=env)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.SubprocessError, OSError) as error:
        print(f"Build setup failed: {error}", file=sys.stderr)
        sys.exit(1)
