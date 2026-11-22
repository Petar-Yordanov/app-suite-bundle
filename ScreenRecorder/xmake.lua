set_project("ScreenRecorder")
set_version("1.0.0")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")

-- Explicit SDK (CI or local override) wins; otherwise use Xmake's Qt packages.
-- Do not mix the SDK provided by CI with a second Qt package installation.
local configured_qt = get_config("qt")
local explicit_sdk = configured_qt and configured_qt ~= "" and configured_qt ~= "auto"
local env_sdk = os.getenv("QT_ROOT_DIR")
if not explicit_sdk and env_sdk and #env_sdk > 0 then
    set_config("qt", env_sdk)
end
local use_qt_packages = not explicit_sdk and not (env_sdk and #env_sdk > 0)
if use_qt_packages then
    add_requires("qt6quick", "qt6widgets")
end
set_warnings("all")
if is_plat("windows") then
    add_defines("NOMINMAX")
    set_runtimes("MD")
    add_cxxflags("/utf-8", {tools = "cl"})
end
if is_plat("linux") then
    add_requires("pkgconfig::gstreamer-app-1.0", {alias = "gstapp"})
    add_requires("pkgconfig::gstreamer-video-1.0", {alias = "gstvideo"})
end

target("screenrecorder")
    add_rules("qt.quickapp")
    add_files("App/*.cpp", "Core/*.cpp", "Platform/Startup.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Recorder.hpp", "App/Updater.hpp", "Core/Feed.hpp", "Platform/Native.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore","QtGui","QtWidgets","QtQml","QtQuick","QtQuickControls2","QtNetwork","QtConcurrent")
    if is_plat("windows") then
        add_files("Platform/Windows.cpp","Platform/Wasapi.cpp")
        add_syslinks("user32","gdi32","ole32","uuid","advapi32","iphlpapi","ws2_32","dxgi")
    elseif is_plat("macosx") then
        add_files("Platform/Mac.mm","Platform/ScreenStream.mm")
        add_mxflags("-fobjc-arc", {force = true})
        add_frameworks("AppKit","AVFoundation","Carbon","CoreGraphics","ScreenCaptureKit","CoreMedia","CoreVideo","AudioToolbox")
        set_values("qt.bundle_identifier", "org.appsuite.screenrecorder")
    elseif is_plat("linux") then
        add_files("Platform/X11.cpp","Platform/Portal.cpp","Platform/PortalRequest.cpp",
                  "Platform/Portal.hpp","Platform/PortalRequest.hpp")
        add_syslinks("X11","Xrandr","Xfixes")
        add_frameworks("QtDBus")
        add_packages("gstapp","gstvideo")
    end
target_end()

target("screenrecorder-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/PlanTests.cpp")
target_end()

target("screenrecorder-media-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Tests/MediaTests.cpp","Core/Ffmpeg.cpp","Core/Feed.cpp","Core/Feed.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore","QtGui","QtNetwork")
    if is_plat("windows") then
        add_syslinks("ws2_32","iphlpapi")
    end
target_end()
