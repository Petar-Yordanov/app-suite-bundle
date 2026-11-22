set_project("DockerGUI")
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
    set_runtimes("MD")
    add_defines("NOMINMAX")
    add_cxxflags("/utf-8", {tools = "cl"})
end

target("dockergui")
    add_rules("qt.quickapp")
    add_files("App/*.cpp", "Core/*.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/ResourceModel.hpp", "App/Updater.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtConcurrent")
    if is_plat("macosx") then
        set_values("qt.bundle_identifier", "org.appsuite.dockergui")
    end
target_end()

target("dockergui-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/HttpTests.cpp")
target_end()

target("dockergui-integration-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Tests/Integration.cpp", "Core/Backend.cpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtNetwork", "QtConcurrent")
target_end()

target("dockergui-qt-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Tests/QtTests.cpp", "Core/Backend.cpp", "App/ResourceModel.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtNetwork", "QtConcurrent")
target_end()
