import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

CheckBox {
    id: control
    font.pixelSize: Theme.Typography.body
    spacing: 7
    implicitHeight: 28
    indicator: Rectangle {
        implicitWidth: 17
        implicitHeight: 17
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 4
        color: control.checked ? Theme.AppTheme.accent : Theme.AppTheme.surface3
        border.color: control.activeFocus ? Theme.AppTheme.accent : Theme.AppTheme.border
        Image {
            width: 13; height: 13
            anchors.centerIn: parent
            source: "qrc:/qml/assets/icons/light/check.png"
            visible: control.checked
            fillMode: Image.PreserveAspectFit
        }
    }
    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.AppTheme.text : Theme.AppTheme.disabledText
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
