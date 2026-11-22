import QtQuick
import "../theme" as Theme

Rectangle {
    id: root
    property string iconName: ""
    property bool destructive: false
    signal clicked()
    activeFocusOnTab: true
    Keys.onReturnPressed: root.clicked()
    Keys.onEnterPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
    implicitWidth: 42
    implicitHeight: 28
    radius: Theme.Metrics.radiusMd
    color: mouse.pressed ? (destructive ? "#bd4545" : Theme.AppTheme.pressed)
         : mouse.containsMouse ? (destructive ? "#d85b5b" : Theme.AppTheme.hover) : "transparent"
    border.color: activeFocus ? Theme.AppTheme.accent :
                  mouse.containsMouse && !destructive ? Theme.AppTheme.menuHighlightBorder : "transparent"
    border.width: activeFocus || (mouse.containsMouse && !destructive) ? 1 : 0
    FIcon { anchors.centerIn: parent; name: root.iconName; iconSize: 14 }
    MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: root.clicked() }
}
