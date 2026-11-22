import QtQuick
import QtQuick.Controls
import "../theme" as Theme

Rectangle {
    id: control
    property string label: "Action"
    property string iconName: ""
    property bool primary: false
    property bool outlined: false
    property int h: 30
    property string tooltipText: ""
    signal triggered()
    signal pressed()

    readonly property bool tall: h >= 46
    activeFocusOnTab: enabled
    Keys.onReturnPressed: if (enabled) control.triggered()
    Keys.onEnterPressed: if (enabled) control.triggered()
    Keys.onSpacePressed: if (enabled) control.triggered()
    implicitHeight: h
    implicitWidth: tall ? Math.max(60, captionTall.implicitWidth + 22)
                        : Math.max(40, captionShort.implicitWidth + (iconName === "" ? 22 : 43))
    height: h
    radius: tall ? Theme.Metrics.radiusMd : Theme.Metrics.radiusSm
    color: !enabled ? "transparent" : pointer.pressed ? Theme.AppTheme.pressed
         : pointer.containsMouse ? Theme.AppTheme.hover
         : primary ? Theme.AppTheme.selectedSoft
         : outlined ? Theme.AppTheme.surface3 : "transparent"
    border.width: activeFocus || (pointer.containsMouse && enabled) || primary || outlined ? 1 : 0
    border.color: activeFocus ? Theme.AppTheme.accent :
                  primary ? Theme.AppTheme.menuHighlightBorder : Theme.AppTheme.border
    opacity: enabled ? 1 : 0.47
    Behavior on color { ColorAnimation { duration: 90 } }

    Column {
        visible: control.tall
        anchors.centerIn: parent
        spacing: 5
        FIcon {
            name: control.iconName
            visible: control.iconName !== ""
            iconSize: 21
            anchors.horizontalCenter: parent.horizontalCenter
        }
        Text {
            id: captionTall
            text: control.label
            color: control.primary ? Theme.AppTheme.accent : Theme.AppTheme.text
            font.pixelSize: Theme.Typography.body
            font.weight: control.primary ? Font.DemiBold : Font.Normal
        }
    }
    Row {
        visible: !control.tall
        anchors.centerIn: parent
        spacing: 6
        FIcon {
            name: control.iconName
            visible: control.iconName !== ""
            iconSize: 15
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            id: captionShort
            text: control.label
            color: control.primary ? Theme.AppTheme.accent : Theme.AppTheme.text
            font.pixelSize: Theme.Typography.body
            font.weight: control.primary ? Font.DemiBold : Font.Normal
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    FToolTip {
        text: control.tooltipText
        visible: control.enabled && pointer.containsMouse && control.tooltipText.length > 0
    }
    MouseArea {
        id: pointer
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: control.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onPressed: if (control.enabled) control.pressed()
        onClicked: if (control.enabled) control.triggered()
    }
}
