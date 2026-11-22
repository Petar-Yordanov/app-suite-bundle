set_project("Arky")
set_version("0.9.0")
set_languages("c++20")
add_rules("mode.debug", "mode.release")

-- Windows SDK headers may otherwise define min/max macros and break std::min.
if is_plat("windows") then
    add_defines("NOMINMAX")
    set_runtimes("MD")
    add_cxxflags("/utf-8", {tools = "cl"})
end

-- Qt 6 Quick Controls 2 and the QML runtime are delivered by the Qt SDK.
-- qt6quick pulls qt6qml; the Qt SDK also supplies Quick Controls 2.
add_requires("libarchive 3.8.9", {configs = {zlib = true, xz = true, bzip2 = true, zstd = true, shared = false}})
add_requires("libzip >=1.11.4", {configs = {shared = false}})

target("ArchiveCore")
    set_kind("static")
    add_files("src/core/archive_engine.cpp")
    add_headerfiles("src/core/archive_engine.hpp")
    add_includedirs("src/core", {public = true})
    add_packages("libarchive", "libzip", {public = true})
    -- Static libarchive's XAR reader/writer uses the Windows XmlLite API.
    if is_plat("windows") then
        add_syslinks("xmllite", {public = true})
    end

target("Arky")
    add_rules("qt.quickapp")
    add_deps("ArchiveCore")
    add_frameworks("QtCore", "QtGui", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork")
    add_files("src/app/*.cpp")
    -- Q_OBJECT headers must be passed through moc explicitly.
    add_files("src/app/archive_controller.hpp", "src/app/archive_model.hpp", "src/app/Updater.hpp")
    add_headerfiles("src/app/*.hpp")
    add_files("src/qml/qml.qrc")
    add_includedirs("src/app", "src/core")
    -- qt6quick supplies QtCore/Gui/Qml/Quick; Qt Quick Controls is loaded
    -- as a QML plugin (not linked as a C++ module). This avoids relying on
    -- a QuickControls2 package that xmake-repo does not expose separately.
    if is_plat("windows") then
        add_syslinks("ole32", "shell32", "user32")
        -- Windows Explorer Details > File description for Arky.exe.
        add_files("src/app/arky.rc")
    end

target("archive_tests")
    set_kind("binary")
    add_deps("ArchiveCore")
    add_files("tests/archive_engine_tests.cpp")

target("mime_tests")
    add_rules("qt.console")
    add_frameworks("QtCore", "QtGui")
    add_deps("ArchiveCore")
    add_files("tests/mime_bridge_tests.cpp", "src/app/archive_model.cpp", "src/app/mime_bridge.cpp", "src/app/export_store.cpp", "src/app/windows_virtual_mime.cpp")
    add_files("src/app/archive_model.hpp")
    add_includedirs("src/app", "src/core")
    -- qt6quick also supplies the QtCore/Gui headers and libraries.
    if is_plat("windows") then
        add_syslinks("ole32", "shell32", "user32")
    end


target("transfer_plan_tests")
    set_kind("binary")
    add_files("tests/transfer_plan_tests.cpp")
    add_includedirs("src/core")

target("arky-cli")
    set_kind("binary")
    add_deps("ArchiveCore")
    add_files("src/cli/main.cpp")
    if is_plat("windows") then
        add_syslinks("shell32")
    end
