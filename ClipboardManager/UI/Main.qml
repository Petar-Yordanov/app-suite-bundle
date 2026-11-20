import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    id: root
    width: 570
    height: 650
    minimumWidth: 410
    minimumHeight: 450
    title: "ClipDeck"
    visible: true
    property bool dark: controller.preferences.theme === "dark" ||
                        (controller.preferences.theme === "system" && Application.styleHints.colorScheme === Qt.Dark)
    color: dark ? "#181c20" : "#f5f6f8"
    palette.window: color
    palette.windowText: dark ? "#f1f4f6" : "#18242a"
    palette.base: dark ? "#252a30" : "#ffffff"
    palette.alternateBase: dark ? "#20252a" : "#e9eef0"
    palette.text: palette.windowText
    palette.button: dark ? "#30383e" : "#e5ebec"
    palette.buttonText: palette.windowText
    palette.highlight: dark ? "#57b8a9" : "#117b70"
    palette.highlightedText: "#ffffff"
    onClosing: function(close) { close.accepted = false; controller.hideUi() }
    Shortcut { sequence: "Escape"; onActivated: controller.hideUi() }

    header: ColumnLayout {
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            height: 70
            color: root.palette.base
            RowLayout {
                anchors.fill: parent; anchors.margins: 16
                ColumnLayout {
                    spacing: 3
                    Label { text: "ClipDeck"; font.pixelSize: 22; font.bold: true }
                    Label { text: "Local clipboard history"; font.pixelSize: 12; opacity: 0.7 }
                }
                Item { Layout.fillWidth: true }
                ToolButton {
                    text: controller.paused ? "Resume" : "Pause"
                    onClicked: controller.paused = !controller.paused
                }
                ToolButton { text: "Hide"; onClicked: controller.hideUi() }
            }
        }
        TabBar {
            id: tabs
            Layout.fillWidth: true
            TabButton { text: "History" }
            TabButton { text: "Settings" }
        }
    }
    StackLayout {
        anchors.fill: parent
        currentIndex: tabs.currentIndex
        ColumnLayout {
            spacing: 9
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 16; Layout.rightMargin: 16; Layout.topMargin: 14
                TextField {
                    id: search
                    placeholderText: "Search clipboard history"
                    Layout.fillWidth: true
                    selectByMouse: true
                    onTextChanged: controller.search(text)
                    Accessible.name: "Search history"
                }
                Button { text: "Clear"; onClicked: confirmClear.open() }
            }
            Label {
                Layout.leftMargin: 16
                Layout.rightMargin: 16
                Layout.fillWidth: true
                text: controller.storageSummary
                opacity: 0.75
                font.pixelSize: 12
            }
            ListView {
                id: history
                Layout.fillWidth: true; Layout.fillHeight: true
                Layout.leftMargin: 12; Layout.rightMargin: 12
                clip: true
                spacing: 6
                boundsBehavior: Flickable.StopAtBounds
                model: controller.entries
                ScrollBar.vertical: ScrollBar { }
                delegate: Rectangle {
                    required property var modelData
                    width: history.width - 10
                    height: 91
                    radius: 8
                    color: root.palette.base
                    border.width: 1
                    border.color: root.dark ? "#343b41" : "#dbe2e5"
                    RowLayout {
                        anchors.fill: parent; anchors.margins: 12
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 5
                            Label {
                                Layout.fillWidth: true
                                text: modelData.kind + "  ·  " + modelData.size
                                font.pixelSize: 11; font.bold: true
                                color: root.palette.highlight
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.preview
                                textFormat: Text.PlainText
                                maximumLineCount: 2
                                wrapMode: Text.WrapAnywhere
                                elide: Text.ElideRight
                                font.pixelSize: 14
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.date
                                font.pixelSize: 11; opacity: 0.65
                                elide: Text.ElideRight
                            }
                        }
                        Button {
                            text: "Copy"
                            onClicked: controller.restore(modelData.id)
                            Accessible.name: "Restore clipboard entry"
                        }
                        ToolButton {
                            text: "×"
                            font.pixelSize: 20
                            onClicked: controller.remove(modelData.id)
                            Accessible.name: "Delete history entry"
                        }
                    }
                }
                Label {
                    anchors.centerIn: parent
                    visible: history.count === 0
                    text: search.text.length ? "No matching entries" : "Copy something to begin"
                    opacity: 0.65
                }
            }
        }
        ScrollView {
            id: settingsView
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: settingsView.availableWidth
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.margins: 16
                    spacing: 10
                    Label { text: "History"; font.bold: true; font.pixelSize: 18 }
                    RowLayout {
                        Label { text: "Maximum entries"; Layout.fillWidth: true }
                        SpinBox { id: maxCount; from: 1; to: 10000; editable: true; value: Number(controller.preferences.maxEntries) }
                    }
                    RowLayout {
                        Label { text: "Maximum storage (MiB)"; Layout.fillWidth: true }
                        SpinBox { id: maxSize; from: 1; to: 8192; editable: true; value: Number(controller.preferences.maxMiB) }
                    }
                    CheckBox { id: persist; text: "Keep history between launches"; checked: controller.preferences.persistent }
                    Label {
                        Layout.fillWidth: true
                        text: "When disabled, snapshots are kept only for this running session and removed when the app exits or next starts."
                        wrapMode: Text.WordWrap
                        font.pixelSize: 12; opacity: 0.75
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.alternateBase }
                    Label { text: "Appearance and access"; font.bold: true; font.pixelSize: 18 }
                    RowLayout {
                        Label { text: "Theme"; Layout.fillWidth: true }
                        ComboBox {
                            id: theme
                            model: ["system", "light", "dark"]
                            currentIndex: Math.max(0, model.indexOf(controller.preferences.theme))
                        }
                    }
                    RowLayout {
                        Label { text: "Global shortcut"; Layout.fillWidth: true }
                        TextField { id: shortcut; text: controller.preferences.shortcut; placeholderText: "Ctrl+Shift+V"; Layout.preferredWidth: 170 }
                    }
                    CheckBox { id: startup; text: "Start automatically at login"; checked: controller.preferences.autostart }
                    Label {
                        text: controller.limitations
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        font.pixelSize: 12; opacity: 0.75
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: root.palette.alternateBase }
                    Label { text: "Updates"; font.bold: true; font.pixelSize: 18 }
                    TextField {
                        id: feed
                        Layout.fillWidth: true
                        text: controller.preferences.updateFeed
                        placeholderText: "Trusted HTTPS release manifest (optional)"
                    }
                    Label {
                        Layout.fillWidth: true; wrapMode: Text.WordWrap
                        text: controller.updater.status; font.pixelSize: 12
                    }
                    RowLayout {
                        Button { text: "Check now"; onClicked: controller.updater.check() }
                        Button { text: "Open update"; enabled: controller.updater.ready; onClicked: controller.updater.install() }
                    }
                    Button {
                        text: controller.settingsBusy ? "Saving…" : "Save settings"
                        enabled: !controller.settingsBusy
                        Layout.fillWidth: true
                        onClicked: controller.applySettings(maxCount.value, maxSize.value, persist.checked, theme.currentText,
                            shortcut.text, startup.checked, feed.text)
                    }
                    Button { text: "Quit ClipDeck"; Layout.fillWidth: true; onClicked: controller.quit() }
                }
            }
        }
    }
    footer: Rectangle {
        height: 40; color: root.palette.base
        Label {
            anchors.fill: parent; anchors.margins: 10
            text: controller.status
            textFormat: Text.PlainText
            elide: Text.ElideRight; font.pixelSize: 12
        }
    }
    Dialog {
        id: confirmClear
        anchors.centerIn: parent
        title: "Clear history?"
        modal: true
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: "All stored clipboard items will be deleted.\nAn active file snapshot is kept until replaced."; wrapMode: Text.WordWrap }
        onAccepted: controller.clear()
    }
}
