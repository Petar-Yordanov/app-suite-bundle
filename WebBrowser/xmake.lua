set_project("WebBrowser")
local app_version = os.getenv("APP_VERSION") or "1.0.0"
set_version(app_version)
set_languages("cxx20")
add_rules("mode.debug", "mode.release")
set_warnings("all")
if is_plat("windows") then
    set_runtimes("MD")
    add_defines("NOMINMAX")
    add_cxxflags("/utf-8", {tools = "cl"})
end
target("webbrowser")
    add_rules("qt.quickapp")
    on_load(function(target)
        -- Validation must run in Xmake's script scope, not description scope.
        local version = os.getenv("APP_VERSION") or "1.0.0"
        if not version:match("^%d+%.%d+%.%d+$") then
            raise("APP_VERSION must be major.minor.patch")
        end
        target:add("defines", 'BROWSER_VERSION="' .. version .. '"')
    end)
    add_files("Core/*.cpp", "App/*.cpp", "App/*.hpp")
    add_frameworks("QtCore", "QtGui", "QtQml", "QtQuick", "QtQuickControls2", "QtNetwork", "QtSql")
    if is_plat("iphoneos") then
        add_files("Platform/IOSView.mm", "Platform/IOSView.hpp", "UI/ios.qrc")
        add_mxflags("-fobjc-arc", {force = true})
        add_frameworks("UIKit", "WebKit", "Foundation")
    else
        add_files("Platform/DesktopProfiles.cpp", "Platform/DesktopProfiles.hpp", "UI/resources.qrc")
        add_frameworks("QtWidgets", "QtWebEngineCore", "QtWebEngineQuick", "QtWebChannel", "QtPositioning")
    end
    if is_plat("macosx", "iphoneos") then set_values("qt.bundle_identifier", "org.appsuite.webbrowser") end
target_end()
target("webbrowser-tests")
    set_kind("binary")
    set_default(false)
    add_files("Core/Rules.cpp", "Tests/RulesTests.cpp")
target_end()
target("webbrowser-store-tests")
    add_rules("qt.console")
    set_default(false)
    add_files("Core/*.cpp", "App/Browser.cpp", "App/Browser.hpp", "Tests/StoreTests.cpp")
    add_frameworks("QtCore", "QtSql")
target_end()
target("webbrowser-bench")
    set_kind("binary")
    set_default(false)
    add_files("Core/Rules.cpp", "Benchmarks/RulesBench.cpp")
target_end()
-- Use Qt's supported static-QML plugin linker and Xcode project generation
-- for the iOS application bundle. Xmake remains the entry point.
task("ios-bundle")
    set_category("plugin")
    on_run(function()
        import("core.base.option")
        local qt = option.get("qt") or os.getenv("QT_ROOT_DIR")
        if not qt then raise("Specify --qt=/path/to/Qt/6.11.3/ios") end
        local simulator = not option.get("device")
        local output = path.absolute(simulator and "build/ios-simulator" or "build/ios-device")
        os.vrunv(path.join(qt,"bin","qt-cmake"), {"-S", ".", "-B",output,"-G","Xcode",
            "-DCMAKE_OSX_SYSROOT=" .. (simulator and "iphonesimulator" or "iphoneos"),
            -- The official Qt iOS SDK uses x86_64 for simulator binaries
            -- and arm64 for physical devices (even on Apple Silicon hosts).
            "-DCMAKE_OSX_ARCHITECTURES=" .. (simulator and "x86_64" or "arm64"),"-DCMAKE_OSX_DEPLOYMENT_TARGET=17.0",
            "-DCMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO"})
        os.vrunv("cmake", {"--build",output,"--config","Release"})
    end)
    set_menu {usage = "xmake ios-bundle [options]", description = "Build native iOS bundle via Qt's static QML linker", options = {
        {nil,"qt","kv",nil,"Qt iOS SDK directory"},
        {'d',"device","k",nil,"Build unsigned device bundle instead of simulator"}
    }}
task_end()
