import QtQuick
import "../theme" as Theme

Rectangle {
    id: control
    property string text: ""
    property string iconName: ""
    signal triggered()
    activeFocusOnTab: enabled
    function activate() {
        control.triggered()
        if (control.parent && control.parent.menuOwner)
            control.parent.menuOwner.close()
    }
    Keys.onReturnPressed: if (enabled) activate()
    Keys.onEnterPressed: if (enabled) activate()
    Keys.onSpacePressed: if (enabled) activate()
    width: parent ? parent.width : 208
    height: 34
    radius: Theme.Metrics.radiusSm
    color: !enabled ? "transparent" : pointer.pressed ? Theme.AppTheme.pressed : pointer.containsMouse ? Theme.AppTheme.menuHighlight : "transparent"
    border.color: activeFocus ? Theme.AppTheme.accent :
                  pointer.containsMouse && enabled ? Theme.AppTheme.menuHighlightBorder : "transparent"
    border.width: activeFocus || (pointer.containsMouse && enabled) ? 1 : 0
    opacity: enabled ? 1 : 0.50
    Row {
        anchors.fill: parent
        anchors.leftMargin: 10
        spacing: 9
        FIcon { name: control.iconName; visible: control.iconName !== ""; iconSize: 15; anchors.verticalCenter: parent.verticalCenter }
        Text { text: control.text; color: Theme.AppTheme.text; font.pixelSize: Theme.Typography.bodyLg; anchors.verticalCenter: parent.verticalCenter }
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        enabled: control.enabled
        cursorShape: Qt.PointingHandCursor
        onClicked: control.activate()
    }
}
