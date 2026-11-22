set_project("ScreenshotTool")
set_version(os.getenv("APP_VERSION") or "1.0.0")
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
    set_runtimes("MD")
    add_cxxflags("/utf-8", {tools = "cl"})
end

target("screenshottool-geometry-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/GeometryTests.cpp")
target_end()

for _, name in ipairs({"screenshottool", "screenshottool-tests", "screenshottool-bench"}) do
    target(name)
        if name == "screenshottool" then
            add_rules("qt.quickapp")
            add_files("AppIcon.qrc")
            if is_plat("windows") then add_files("AppIcon.rc") end
            on_load(function(target)
                local version = os.getenv("APP_VERSION") or "1.0.0"
                if not version:match("^%d+%.%d+%.%d+$") then raise("Invalid APP_VERSION") end
                target:add("defines", 'APP_SUITE_VERSION="' .. version .. '"')
                local repo = os.getenv("GITHUB_REPOSITORY") or ""
                if repo:match("^[%w_.-]+/[%w_.-]+$") then
                    target:add("defines", 'APP_SUITE_FEED="https://github.com/' .. repo .. '/releases/download/latest-builds/ScreenshotTool-update.json"')
                end
            end)
            add_files("App/main.cpp")
        else
            add_rules("qt.console")
            set_default(false)
            if name == "screenshottool-tests" then
                add_files("Tests/Tests.cpp")
            else
                add_files("Benchmarks/Bench.cpp")
            end
        end
        add_files("Core/*.cpp", "Platform/Startup.cpp", "App/Controller.cpp", "App/Updater.cpp", "UI/resources.qrc")
        add_files("App/Controller.hpp", "App/Updater.hpp", "Platform/Native.hpp")
        if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtConcurrent")
        if is_plat("windows") then
            add_files("Platform/Windows.cpp")
            add_syslinks("user32", "gdi32", "advapi32", "d3d11", "dxgi", "windowsapp")
        elseif is_plat("macosx") then
            add_files("Platform/Mac.mm")
            add_frameworks("AppKit", "Carbon", "CoreGraphics", "ScreenCaptureKit")
        elseif is_plat("linux") then
            add_files("Platform/X11.cpp", "Platform/Portal.cpp", "Platform/Portal.hpp", "Platform/PortalRequest.hpp")
            add_syslinks("X11", "Xrandr", "Xfixes")
            add_frameworks("QtDBus")
        end
    target_end()
end
