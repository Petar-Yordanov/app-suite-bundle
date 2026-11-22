# Third-party components

- Qt 6 (Core, Gui, Qml, Quick, Quick Controls, Sql, Network, Widgets desktop, WebEngine, WebChannel, Positioning), subject to Qt licensing for the particular modules and build. Review LGPL/GPL and Chromium third-party notices for your distribution; Qt WebEngine bundles Chromium and many dependencies.
- SQLite via Qt's QSQLite driver.
- Apple WebKit/UIKit/Foundation on iOS under Apple SDK/system terms.
- Xmake build tooling; Qt CMake/Xcode static plugin deployment for iOS.
- Optional Python psutil for benchmark harness only.

No proprietary Widevine CDM is bundled. No ad-block subscription or externally maintained rules list is bundled. The packaged Qt libraries come from the selected SDK; retain their accompanying license notices and fulfill applicable redistribution obligations before public releases. See https://doc.qt.io/qt-6/licensing.html and https://doc.qt.io/qt-6/qtwebengine-licensing.html.
