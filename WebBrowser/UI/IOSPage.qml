import QtQuick
import Browser.Native 1.0

NativeWebView {
    id: page
    required property var browserRoot
    property bool current: false
    property bool covered: false
    shown: current && !covered
}
