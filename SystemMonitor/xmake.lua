set_project("SystemMonitor")
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
    add_defines("NOMINMAX", "_WIN32_WINNT=0x0A00", "WINVER=0x0A00")
    add_cxxflags("/utf-8", {tools = "cl"})
end
local function native()
    add_files("Core/*.cpp", "Platform/Nvidia.cpp")
    if is_plat("windows") then
        add_files("Platform/Windows.cpp", "Platform/WindowsEtw.cpp")
        add_syslinks("kernel32", "advapi32", "user32", "psapi", "pdh", "dxgi", "shell32", "ole32", "uuid", "tdh")
    elseif is_plat("macosx") then
        add_files("Platform/Mac.cpp", "Platform/MacNetwork.cpp")
        add_frameworks("IOKit", "CoreFoundation")
    else
        add_files("Platform/Linux.cpp", "Platform/LinuxTcp.cpp")
        add_syslinks("dl", "pthread")
    end
end
target("systemmonitor")
    add_rules("qt.quickapp")
    add_files("AppIcon.qrc")
    if is_plat("windows") then add_files("AppIcon.rc") end
    on_load(function(target)
        local version = os.getenv("APP_VERSION") or "1.0.0"
        if not version:match("^%d+%.%d+%.%d+$") then raise("Invalid APP_VERSION") end
        target:add("defines", 'APP_SUITE_VERSION="' .. version .. '"')
        local repo = os.getenv("GITHUB_REPOSITORY") or ""
        if repo:match("^[%w_.-]+/[%w_.-]+$") then
            target:add("defines", 'APP_SUITE_FEED="https://github.com/' .. repo .. '/releases/download/latest-builds/SystemMonitor-update.json"')
        end
    end)
    native()
    add_files("App/*.cpp", "App/Controller.hpp", "App/Models.hpp", "App/Updater.hpp", "UI/resources.qrc")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtConcurrent")
    if is_plat("macosx") then set_values("qt.bundle_identifier", "org.appsuite.systemmonitor") end
target_end()
target("systemmonitor-tests")
    set_kind("binary")
    set_default(false)
    native()
    add_files("Tests/NativeTests.cpp")
target_end()
target("systemmonitor-bench")
    set_kind("binary")
    set_default(false)
    native()
    add_files("Benchmarks/NativeBench.cpp")
target_end()
target("systemmonitor-model-tests")
    add_rules("qt.console")
    set_default(false)
    native()
    add_files("Tests/ModelTests.cpp", "App/Models.cpp", "App/Models.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore")
target_end()
