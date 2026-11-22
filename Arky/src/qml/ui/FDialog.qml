import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../theme" as Theme

Dialog {
    id: dialog
    modal: true
    padding: 18
    palette.window: Theme.AppTheme.popupBg
    palette.windowText: Theme.AppTheme.text
    anchors.centerIn: parent
    background: Rectangle {
        radius: Theme.Metrics.radiusLg
        color: Theme.AppTheme.popupBg
        border.color: Theme.AppTheme.border
        border.width: 1
    }
    header: Rectangle {
        implicitHeight: 48
        radius: Theme.Metrics.radiusLg
        color: Theme.AppTheme.surface2
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.AppTheme.borderSoft }
        Text {
            anchors.left: parent.left
            anchors.leftMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            text: dialog.title
            color: Theme.AppTheme.text
            font.pixelSize: Theme.Typography.subtitle
            font.weight: Font.DemiBold
        }
    }
    footer: Rectangle {
        implicitHeight: 54
        color: Theme.AppTheme.surface2
        radius: Theme.Metrics.radiusLg
        Rectangle { width: parent.width; height: 1; color: Theme.AppTheme.borderSoft }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 18
            anchors.rightMargin: 18
            spacing: 8
            Item { Layout.fillWidth: true }
            FButton {
                label: "Cancel"
                outlined: true
                visible: !!(dialog.standardButtons & Dialog.Cancel)
                onTriggered: dialog.reject()
            }
            FButton {
                label: (dialog.standardButtons & Dialog.Yes) ? "Yes" :
                       (dialog.standardButtons & Dialog.Close) && !(dialog.standardButtons & Dialog.Ok) ? "Close" : "OK"
                primary: true
                onTriggered: (dialog.standardButtons & Dialog.Close) && !(dialog.standardButtons & Dialog.Ok) && !(dialog.standardButtons & Dialog.Yes)
                             ? dialog.reject() : dialog.accept()
            }
        }
    }
}
