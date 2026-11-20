set_project("ScreenshotTool")
set_version("1.0.0")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")
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
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtConcurrent")
        if is_plat("windows") then
            add_files("Platform/Windows.cpp")
            add_syslinks("user32", "gdi32", "advapi32")
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
