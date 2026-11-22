set_project("ClipDeck")
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

set_warnings("all")

option("desktop")
    set_default(true)
    set_showmenu(true)
    set_description("Build the Qt desktop application")
option_end()

target("clipdeck-core")
    set_kind("static")
    add_files("Core/Store.cpp")
    add_includedirs("Core", {public = true})
    set_default(false)
target_end()

if has_config("desktop") then
    if use_qt_packages then add_requires("qt6quick", "qt6widgets") end
    target("clipdeck")
        add_rules("qt.quickapp")
        add_deps("clipdeck-core")
        add_files("App/*.cpp", "Platform/Native.cpp", "UI/resources.qrc")
        add_files("App/Controller.hpp", "App/Updater.hpp", "Platform/Native.hpp")
        if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork")
        if is_plat("windows") then
            add_syslinks("user32", "advapi32")
        elseif is_plat("macosx") then
            add_files("Platform/Mac.mm")
            add_frameworks("AppKit", "Carbon")
        elseif is_plat("linux") then
            add_syslinks("X11")
        end
    target_end()
    target("clipdeck-integration-tests")
        add_rules("qt.quickapp")
        set_default(false)
        add_deps("clipdeck-core")
        add_files("Tests/QtIntegrationTests.cpp", "App/Controller.cpp", "App/Updater.cpp", "Platform/Native.cpp", "UI/resources.qrc")
        add_files("App/Controller.hpp", "App/Updater.hpp", "Platform/Native.hpp")
        if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork")
        if is_plat("windows") then
            add_syslinks("user32", "advapi32")
        elseif is_plat("macosx") then
            add_files("Platform/Mac.mm")
            add_frameworks("AppKit", "Carbon")
        elseif is_plat("linux") then
            add_syslinks("X11")
        end
    target_end()
end

target("clipdeck-tests")
    set_kind("binary")
    add_files("Tests/StoreTests.cpp")
    add_deps("clipdeck-core")
    set_default(false)
target_end()

target("clipdeck-bench")
    set_kind("binary")
    add_files("Benchmarks/StoreBench.cpp")
    add_deps("clipdeck-core")
    set_default(false)
target_end()
