set_project("ImageViewer")
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

-- Project-local recipe: upstream xmake-repo treats the aom_decoder option
-- as a package name, which does not exist. Build both actual decoder
-- dependencies directly and disable runtime plugin loading so the deployed
-- executable does not need codecs from the CI machine's package cache.
package("imageviewer-libheif")
    set_homepage("https://github.com/strukturag/libheif")
    set_description("libheif with built-in HEVC and AV1 decoders")
    set_license("LGPL-3.0")
    add_urls("https://github.com/strukturag/libheif/releases/download/v$(version)/libheif-$(version).tar.gz")
    add_versions("1.23.4", "d0c02b4b0e978f34a1974b6f3eea7975a537bf7a9195ffeea38e7242ff316fdd")
    add_deps("cmake")
    add_deps("libde265 1.1.1", {configs = {shared = false, tools = false}})
    add_links("heif")
    on_load(function (package)
        if package:is_plat("windows") then
            package:add("defines", "LIBHEIF_STATIC_BUILD")
        end
        -- The dav1d Meson toolchain pulls an Xmake-built Python on macOS.
        -- Use the real AOM package (CMake), not the nonexistent aom_decoder
        -- package from the upstream libheif recipe, on Apple silicon.
        if package:is_plat("macosx") then
            package:add("deps", "aom 3.13.1", {configs = {shared = false, tools = false}})
        else
            package:add("deps", "dav1d 1.5.0", {configs = {shared = false, tools = false}})
        end
    end)
    on_install("windows", "linux", "macosx", function (package)
        local use_aom = package:is_plat("macosx")
        local configs = {
            "-DBUILD_SHARED_LIBS=OFF",
            "-DENABLE_PLUGIN_LOADING=OFF",
            "-DWITH_LIBDE265=ON",
            "-DWITH_LIBDE265_PLUGIN=OFF",
            "-DWITH_DAV1D=" .. (use_aom and "OFF" or "ON"),
            "-DWITH_DAV1D_PLUGIN=OFF",
            "-DWITH_AOM_DECODER=" .. (use_aom and "ON" or "OFF"),
            "-DWITH_AOM_DECODER_PLUGIN=OFF",
            "-DWITH_AOM_ENCODER=OFF",
            "-DWITH_X265=OFF",
            "-DWITH_RAV1E=OFF",
            "-DWITH_LIBSHARPYUV=OFF",
            "-DWITH_HEADER_COMPRESSION=OFF",
            "-DWITH_UNCOMPRESSED_CODEC=OFF",
            "-DWITH_GDK_PIXBUF=OFF",
            "-DWITH_EXAMPLES=OFF",
            "-DBUILD_TESTING=OFF",
            "-DBUILD_DOCUMENTATION=OFF"
        }
        local opt = {}
        if package:is_plat("windows") then
            -- libde265 is static: its headers must not mark calls as dllimport.
            -- Apply this while compiling libheif itself, not merely the viewer.
            opt.cxflags = {"-DLIBDE265_STATIC_BUILD"}
        end
        import("package.tools.cmake").install(package, configs, opt)
    end)
    on_test(function (package)
        assert(package:has_cfuncs("heif_have_decoder_for_format", {includes = "libheif/heif.h"}))
    end)
package_end()

add_requires("imageviewer-libheif 1.23.4", {system = false, configs = {shared = false}})
add_requires("libraw 0.22.2", {system = false, configs = {shared = false}})
add_requires("stb 2026.03.18", {system = false})

target("imageviewer")
    add_rules("qt.quickapp")
    add_files("Core/*.cpp", "App/*.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Updater.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtSvg")
    add_packages("imageviewer-libheif", "libraw", "stb")
target_end()

target("imageviewer-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Core/*.cpp", "App/Controller.cpp", "App/Updater.cpp", "Tests/IntegrationTests.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Updater.hpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtSvg")
    add_packages("imageviewer-libheif", "libraw", "stb")
target_end()

target("imageviewer-metadata-tests")
    set_kind("binary")
    set_default(false)
    add_files("Core/Metadata.cpp", "Tests/MetadataTests.cpp")
target_end()

target("imageviewer-bench")
    add_rules("qt.console")
    set_default(false)
    add_files("Core/*.cpp", "Benchmarks/DecodeBench.cpp")
    if use_qt_packages then add_packages("qt6quick", "qt6widgets") end
    add_frameworks("QtCore", "QtGui", "QtSvg")
    add_packages("imageviewer-libheif", "libraw", "stb")
target_end()
