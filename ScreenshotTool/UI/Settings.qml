import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: root
    width: 470; height: 680; minimumWidth: 390; minimumHeight: 420
    visible: false
    title: "Screenshot Tool"
    onClosing: shot.uiClosed()
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 10
            Label { text: "Capture"; font.pixelSize: 21; font.bold: true; Layout.leftMargin: 16; Layout.topMargin: 14 }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                ComboBox { id: screen; model: shot.screens; Layout.fillWidth: true; enabled: !shot.portal }
                Button { text: "Refresh"; onClicked: shot.refreshTargets(); enabled: !shot.portal }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                Button { text: "Region"; Layout.fillWidth: true; onClicked: shot.capture(0,screen.currentIndex); enabled: !shot.busy }
                Button { text: "Screen"; Layout.fillWidth: true; onClicked: shot.capture(1,screen.currentIndex); enabled: !shot.busy }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                ComboBox { id: win; model: shot.windows; Layout.fillWidth: true; enabled: !shot.portal }
                Button { text: "Window"; enabled: !shot.busy && (shot.portal || win.currentIndex >= 0); onClicked: shot.capture(2,win.currentIndex) }
            }
            RowLayout {
                Layout.leftMargin: 16
                Label { text: "Delay (seconds)" }
                SpinBox { from: 0; to: 60; value: shot.delay; onValueModified: shot.delay = value }
            }
            CheckBox { text: "Include mouse cursor"; checked: shot.cursor; enabled: !shot.portal; onToggled: shot.cursor = checked; Layout.leftMargin: 12 }
            Label { text: "Global shortcuts"; font.pixelSize: 18; font.bold: true; Layout.leftMargin: 16 }
            GridLayout {
                columns: 2; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                Label { text: "Region" }
                TextField { id: regionKey; text: shot.shortcuts[0]; Layout.fillWidth: true }
                Label { text: "Screen" }
                TextField { id: screenKey; text: shot.shortcuts[1]; Layout.fillWidth: true }
                Label { text: "Window" }
                TextField { id: windowKey; text: shot.shortcuts[2]; Layout.fillWidth: true }
            }
            Button { text: "Apply shortcuts"; onClicked: shot.setShortcuts(regionKey.text,screenKey.text,windowKey.text); Layout.leftMargin: 16 }
            Label { text: "Settings"; font.pixelSize: 18; font.bold: true; Layout.leftMargin: 16 }
            CheckBox { text: "Start at login"; checked: shot.autostart; onToggled: shot.autostart = checked; Layout.leftMargin: 12 }
            RowLayout {
                Layout.leftMargin: 16
                Label { text: "Theme" }
                ComboBox {
                    model: ["system","light","dark"]
                    currentIndex: model.indexOf(shot.theme)
                    onActivated: shot.theme = currentText
                }
            }
            Label { text: "Updates"; font.pixelSize: 18; font.bold: true; Layout.leftMargin: 16 }
            RowLayout {
                Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16
                TextField { id: feed; text: shot.updateFeed; placeholderText: "HTTPS release manifest"; Layout.fillWidth: true }
                Button { text: "Apply"; onClicked: shot.updateFeed = feed.text }
            }
            RowLayout {
                Layout.leftMargin: 16
                Button { text: "Check"; onClicked: shot.updater.check() }
                Button { text: "Install"; enabled: shot.updater.ready; onClicked: shot.updater.install() }
            }
            Label { text: shot.updater.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16 }
            Label { text: shot.limitations; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; opacity: 0.75 }
            Label { text: shot.status; wrapMode: Text.WordWrap; Layout.fillWidth: true; Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.bottomMargin: 16 }
        }
    }
}
