import QtQuick
import "../theme" as Theme

Item {
    id: control
    property string name: ""
    property int iconSize: 16
    property real iconOpacity: 1.0
    implicitWidth: iconSize
    implicitHeight: iconSize
    width: implicitWidth
    height: implicitHeight
    Image {
        anchors.fill: parent
        source: control.name === "" ? "" : "qrc:/qml/assets/icons/" +
                (Theme.AppTheme.isDark ? "light/" : "dark/") + control.name + ".png"
        fillMode: Image.PreserveAspectFit
        smooth: true
        mipmap: true
        opacity: control.iconOpacity
        asynchronous: true
    }
}
