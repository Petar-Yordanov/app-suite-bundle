set_project("TerminalEmulator")
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
    add_defines("NOMINMAX", "_WIN32_WINNT=0x0A00")
    add_cxxflags("/utf-8", {tools = "cl"})
end

-- The release archive contains the generated encoding tables. No Perl,
-- autotools or Unix build scripts are needed on Windows.
package("appsuite-vterm")
    set_homepage("https://www.leonerd.org.uk/code/libvterm/")
    set_description("Pinned standalone libvterm terminal emulator")
    set_license("MIT")
    add_urls("https://launchpad.net/libvterm/trunk/v0.3/+download/libvterm-$(version).tar.gz",
             "https://distfiles.macports.org/libvterm/libvterm-$(version).tar.gz",
             "https://deb.debian.org/debian/pool/main/libv/libvterm/libvterm_$(version).orig.tar.gz")
    add_versions("0.3.3", "09156f43dd2128bd347cbeebe50d9a571d32c64e0cf18d211197946aff7226e0")
    add_links("vterm")
    on_install("windows", "linux", "macosx", function (package)
        io.writefile("xmake.lua", [[
            set_languages("c11")
            target("vterm")
                set_kind("static")
                add_files("src/*.c")
                add_includedirs("include")
                add_headerfiles("include/*.h")
                if is_plat("windows") then
                    -- Use the MSVC-ABI Clang C compiler for C99 compound literals.
                    set_toolchains("clang-cl")
                    set_runtimes("MD")
                    add_defines("_CRT_SECURE_NO_WARNINGS")
                end
            target_end()
        ]])
        import("package.tools.xmake").install(package)
    end)
    on_test(function (package)
        assert(package:has_cfuncs("vterm_new", {includes = "vterm.h"}))
    end)
package_end()
add_requires("appsuite-vterm 0.3.3", {system = false, configs = {shared = false}})

local function add_pty()
    if is_plat("windows") then
        add_files("Platform/WindowsPty.cpp")
        add_syslinks("kernel32")
    else
        add_files("Platform/PosixPty.cpp")
        if is_plat("linux") then add_syslinks("util", "pthread") end
    end
end

local function add_terminal_core()
    add_files("Core/Emulator.cpp")
    add_packages("appsuite-vterm")
    add_pty()
end

target("terminalemulator")
    add_rules("qt.quickapp")
    add_terminal_core()
    add_files("App/*.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Layout.hpp", "App/Session.hpp", "App/TerminalItem.hpp", "App/Updater.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork")
    if is_plat("macosx") then set_values("qt.bundle_identifier", "org.appsuite.terminalemulator") end
target_end()

target("terminal-pty-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/PtyTests.cpp")
    add_pty()
target_end()

target("terminal-vt-tests")
    set_kind("binary")
    set_default(false)
    add_files("Tests/EmulationTests.cpp", "Core/Emulator.cpp")
    add_packages("appsuite-vterm")
target_end()

target("terminal-layout-tests")
    add_rules("qt.console")
    set_default(false)
    add_terminal_core()
    add_files("Tests/LayoutTests.cpp", "App/Controller.cpp", "App/Layout.cpp", "App/Session.cpp", "App/Shells.cpp")
    add_files("App/Controller.hpp", "App/Layout.hpp", "App/Session.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtNetwork")
target_end()
