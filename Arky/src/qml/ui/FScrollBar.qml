import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

ScrollBar {
    id: control
    policy: ScrollBar.AsNeeded
    implicitWidth: orientation === Qt.Vertical ? 10 : 0
    implicitHeight: orientation === Qt.Horizontal ? 10 : 0
    minimumSize: 0.08
    contentItem: Rectangle {
        implicitWidth: 6
        radius: width / 2
        color: control.pressed ? Theme.AppTheme.scrollbarThumbPressed : control.hovered ? Theme.AppTheme.scrollbarThumbHover : Theme.AppTheme.scrollbarThumb
    }
    background: Rectangle { color: Theme.AppTheme.scrollbarTrack; radius: 5 }
}
