import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 640; height: 460; minimumWidth: 320; minimumHeight: 260
    visible: false; title: "Screenshot Preview"
    onClosing: shot.uiClosed()
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        Rectangle {
            Layout.fillHeight: true; Layout.fillWidth: true; color: "#202020"; clip: true
            Image { anchors.fill: parent; source: "image://capture/current"; sourceSize.width: 1280; sourceSize.height: 1280; fillMode: Image.PreserveAspectFit; cache: false }
        }
        Label { text: shot.imageWidth + " x " + shot.imageHeight + " px"; opacity: 0.8 }
        Label { text: shot.status; Layout.fillWidth: true; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Copy"; onClicked: shot.copy() }
            Button { text: "Save PNG..."; onClicked: dialog.open() }
            Item { Layout.fillWidth: true }
            Button { text: "Close"; onClicked: root.close() }
        }
    }
    FileDialog { id: dialog; title: "Save screenshot"; fileMode: FileDialog.SaveFile; nameFilters: ["PNG image (*.png)"]; defaultSuffix: "png"; onAccepted: shot.save(selectedFile) }
    Shortcut { sequence: "Escape"; onActivated: root.close() }
    Shortcut { sequence: StandardKey.Copy; onActivated: shot.copy() }
    Shortcut { sequence: StandardKey.Save; onActivated: dialog.open() }
}
