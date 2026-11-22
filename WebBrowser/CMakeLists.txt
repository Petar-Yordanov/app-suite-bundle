# Used only by the Xmake ios-bundle task for Qt static plugin linking.
cmake_minimum_required(VERSION 3.24)
project(WebBrowser VERSION 1.0.0 LANGUAGES CXX OBJCXX)
if(DEFINED ENV{APP_VERSION})
    if(NOT "$ENV{APP_VERSION}" MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "APP_VERSION must be major.minor.patch")
    endif()
    set(PROJECT_VERSION "$ENV{APP_VERSION}")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD 20)
find_package(Qt6 6.11 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network Sql)
qt_standard_project_setup(REQUIRES 6.11)
qt_add_executable(webbrowser MANUAL_FINALIZATION
    App/main.cpp App/Browser.cpp App/Browser.hpp App/Downloads.cpp App/Downloads.hpp
    App/Updater.cpp App/Updater.hpp Core/Rules.cpp Core/Store.cpp
    Platform/IOSView.mm Platform/IOSView.hpp)
target_compile_definitions(webbrowser PRIVATE BROWSER_VERSION="${PROJECT_VERSION}")
set_source_files_properties(Platform/IOSView.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
qt_add_resources(webbrowser ios_ui FILES UI/Main.qml UI/IOSPage.qml UI/Compatibility.html PREFIX "/" BASE UI)
qt_add_qml_module(webbrowser URI BrowserUI VERSION 1.0
    QML_FILES UI/Main.qml UI/IOSPage.qml
    NO_CACHEGEN NO_LINT)
target_link_libraries(webbrowser PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Network Qt6::Sql "-framework WebKit" "-framework UIKit" "-framework Foundation")
qt_import_plugins(webbrowser INCLUDE Qt6::QSQLiteDriverPlugin)
set_target_properties(webbrowser PROPERTIES MACOSX_BUNDLE TRUE MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Packaging/iOS-Info.plist" MACOSX_BUNDLE_GUI_IDENTIFIER org.appsuite.webbrowser XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER org.appsuite.webbrowser XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2")
qt_finalize_executable(webbrowser)
