import QtQuick
import "../theme" as Theme

Rectangle {
    id: root
    property string text: "Menu"
    signal triggered()
    activeFocusOnTab: true
    Keys.onReturnPressed: root.triggered()
    Keys.onEnterPressed: root.triggered()
    Keys.onSpacePressed: root.triggered()
    implicitWidth: label.implicitWidth + 24
    implicitHeight: 28
    radius: Theme.Metrics.radiusSm
    color: pointer.pressed ? Theme.AppTheme.pressed : pointer.containsMouse ? Theme.AppTheme.hover : "transparent"
    border.width: pointer.containsMouse || activeFocus ? 1 : 0
    border.color: activeFocus ? Theme.AppTheme.accent : Theme.AppTheme.border
    Text {
        id: label
        text: root.text
        anchors.centerIn: parent
        color: Theme.AppTheme.text
        font.pixelSize: Theme.Typography.bodyLg
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.triggered()
    }
}
