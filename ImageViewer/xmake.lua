set_project("ImageViewer")
set_version("1.0.0")
set_languages("cxx20")
add_rules("mode.debug", "mode.release")
set_warnings("all")
if is_plat("windows") then
    set_runtimes("MD")
end

-- Only decoding libraries; no encoders, Qt WebEngine, or network service.
add_requires("libheif 1.23.4", {system = false, configs = {shared = false, libde265 = true, aom_decoder = true}})
add_requires("libraw 0.22.2", {system = false, configs = {shared = false}})
add_requires("stb 2026.03.18", {system = false})

target("imageviewer")
    add_rules("qt.quickapp")
    add_files("Core/*.cpp", "App/*.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Updater.hpp")
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtSvg")
    add_packages("libheif", "libraw", "stb")
target_end()

target("imageviewer-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Core/*.cpp", "App/Controller.cpp", "App/Updater.cpp", "Tests/IntegrationTests.cpp", "UI/resources.qrc")
    add_files("App/Controller.hpp", "App/Updater.hpp")
    add_frameworks("QtCore", "QtGui", "QtWidgets", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtSvg")
    add_packages("libheif", "libraw", "stb")
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
    add_frameworks("QtCore", "QtGui", "QtSvg")
    add_packages("libheif", "libraw", "stb")
target_end()
