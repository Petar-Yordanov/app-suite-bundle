set_project("MediaPlayer")
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
    add_cxxflags("/utf-8", {tools = "cl"})
end

local bundled_include = path.join(os.projectdir(), ".deps", "vlc", "vlc-3.0.24", "include")
local function detect_vlc_include()
    local explicit = os.getenv("VLC_INCLUDE_DIR")
    if explicit and #explicit > 0 then return explicit end
    for _, dir in ipairs({bundled_include, "/usr/include", "/usr/local/include", "/opt/homebrew/include"}) do
        if os.isfile(path.join(dir, "vlc", "vlc.h")) then return dir end
    end
    return ""
end
option("vlc_include")
    set_default(detect_vlc_include())
    set_showmenu(true)
    set_description("Directory containing the official libVLC 3 vlc/vlc.h headers")
option_end()

local function detect_vlc_root()
    local explicit = os.getenv("VLC_SDK_ROOT")
    if explicit and #explicit > 0 then return explicit end
    if is_plat("windows") then
        local dlls = os.files(path.join(os.projectdir(), ".deps", "vlc", "runtime", "**", "libvlc.dll"))
        if #dlls > 0 then return path.directory(dlls[1]) end
        local installed = path.join(os.getenv("ProgramFiles") or "C:/Program Files", "VideoLAN", "VLC")
        if os.isfile(path.join(installed, "libvlc.dll")) then return installed end
    elseif is_plat("macosx") then
        local installed = path.join(os.projectdir(), ".deps", "vlc", "VLC.app", "Contents", "MacOS")
        if os.isdir(installed) then return installed end
    end
    return ""
end
option("vlc_root")
    set_default(detect_vlc_root())
    set_showmenu(true)
    set_description("Path to matching libVLC runtime (optional with a system installation)")
option_end()

target("mediaplayer-core-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/AlgorithmsTests.cpp")
target_end()

for _, name in ipairs({"mediaplayer", "mediaplayer-tests"}) do
    target(name)
        if name == "mediaplayer" then
            add_rules("qt.quickapp")
            add_files("App/main.cpp")
        else
            add_rules("qt.console")
            set_default(false)
            add_files("Tests/IntegrationTests.cpp")
        end
        add_files("Core/*.cpp", "App/Player.cpp", "App/VideoSurface.cpp", "App/MainWindow.cpp", "App/Updater.cpp", "UI/resources.qrc")
        add_files("App/Player.hpp", "App/Updater.hpp")
        if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
        add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickWidgets", "QtQuickControls2", "QtNetwork")
        local headers = get_config("vlc_include")
        if headers and #headers > 0 then
            add_includedirs(headers)
        end
        local runtime = get_config("vlc_root")
        if runtime and #runtime > 0 then
            add_runenvs("VLC_SDK_ROOT", runtime)
            if is_plat("windows") then add_runenvs("PATH", runtime) end
        end
        if is_plat("linux") then
            add_syslinks("X11")
        elseif is_plat("macosx") then
            add_ldflags("-Wl,-rpath,@executable_path/lib", {force = true})
        end
    target_end()
end
