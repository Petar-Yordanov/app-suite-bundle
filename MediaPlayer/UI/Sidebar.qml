import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    padding: 10
    component CompactButton: Button { implicitWidth: Math.max(40,contentItem.implicitWidth+12); implicitHeight: 32; padding: 4 }
    ColumnLayout {
        anchors.fill: parent
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: player.panel === "playlist" ? "Playlist" : player.panel === "tracks" ? "Audio and subtitles" : "Settings"; font.bold: true; font.pixelSize: 18; Layout.fillWidth: true }
            ToolButton { text: "Close"; onClicked: player.setPanel("") }
        }
        Loader {
            Layout.fillHeight: true; Layout.fillWidth: true
            sourceComponent: player.panel === "playlist" ? playlistPanel : player.panel === "tracks" ? trackPanel : player.panel === "settings" ? settingsPanel : null
        }
    }
    Component {
        id: playlistPanel
        ColumnLayout {
            id: listPanel
            RowLayout {
                Layout.fillWidth: true
                Button { text: "Add files"; onClicked: player.addDialog(); Layout.fillWidth: true }
                Button { text: "Clear"; enabled: player.playlist.length > 0; onClicked: player.clearPlaylist() }
            }
            Label { text: player.playlist.length === 0 ? "No playlist. Prev / Next use the current folder." : player.playlist.length + " videos"; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            ListView {
                id: links
                Layout.fillWidth: true; Layout.fillHeight: true; clip: true
                model: player.playlist
                currentIndex: player.playlistIndex
                ScrollBar.vertical: ScrollBar {}
                delegate: ItemDelegate {
                    required property string modelData
                    required property int index
                    width: links.width
                    text: modelData.replace(/\\/g, "/").split("/").pop()
                    highlighted: links.currentIndex === index
                    onClicked: links.currentIndex = index
                    onDoubleClicked: player.playIndex(index)
                    ToolTip.visible: hovered; ToolTip.text: modelData
                }
            }
            RowLayout {
                Layout.fillWidth: true
                CompactButton { text: "Play"; enabled: links.currentIndex >= 0; onClicked: player.playIndex(links.currentIndex) }
                CompactButton { text: "Up"; enabled: links.currentIndex > 0; onClicked: { var n=links.currentIndex-1; player.moveIndex(links.currentIndex,n); links.currentIndex=n } }
                CompactButton { text: "Down"; enabled: links.currentIndex >= 0 && links.currentIndex < player.playlist.length-1; onClicked: { var n=links.currentIndex+1; player.moveIndex(links.currentIndex,n); links.currentIndex=n } }
                CompactButton { text: "Remove"; enabled: links.currentIndex >= 0; onClicked: player.removeIndex(links.currentIndex) }
            }
        }
    }
    Component {
        id: trackPanel
        ScrollView {
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width
                Label { text: "Audio track"; font.bold: true }
                ComboBox {
                    Layout.fillWidth: true; model: player.audioTracks; textRole: "label"; valueRole: "id"
                    currentIndex: indexOfValue(player.audioTrack); enabled: player.audioTracks.length > 0
                    onActivated: player.setAudioTrack(currentValue)
                }
                Label { text: "Subtitle track"; font.bold: true }
                ComboBox {
                    Layout.fillWidth: true; model: player.subtitleTracks; textRole: "label"; valueRole: "id"
                    currentIndex: indexOfValue(player.subtitleTrack); enabled: player.subtitleTracks.length > 0
                    onActivated: player.setSubtitleTrack(currentValue)
                }
                Button { text: "Load subtitle file..."; Layout.fillWidth: true; enabled: player.hasMedia && !player.busy; onClicked: player.addSubtitleDialog() }
                Label { text: "Aspect ratio"; font.bold: true }
                ComboBox {
                    model: ["Original","4:3","16:9","16:10","21:9","239:100","1:1"]
                    Layout.fillWidth: true; currentIndex: model.indexOf(player.aspect); onActivated: player.aspect = currentText
                }
                Label { text: player.rendering; wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.7 }
            }
        }
    }
    Component {
        id: settingsPanel
        ScrollView {
            contentWidth: availableWidth; clip: true
            ColumnLayout {
                width: parent.width
                Label { text: "Theme"; font.bold: true }
                ComboBox { model: ["system","light","dark"]; Layout.fillWidth: true; currentIndex: model.indexOf(player.theme); onActivated: player.theme = currentText }
                CheckBox { text: "Remember playback position"; checked: player.remember; onToggled: player.remember = checked }
                Label { text: "Keyboard shortcuts"; font.bold: true }
                Label { text: "Type a shortcut in Qt notation, e.g. Ctrl+P. Escape exits fullscreen."; wrapMode: Text.WordWrap; Layout.fillWidth: true; opacity: 0.75 }
                Repeater {
                    model: player.shortcutActions
                    delegate: ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.label }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { id: shortcutField; text: modelData.key; Layout.fillWidth: true; selectByMouse: true }
                            Button { text: "Set"; onClicked: player.setShortcut(modelData.id,shortcutField.text) }
                        }
                    }
                }
                Button { text: "Reset shortcuts"; onClicked: player.resetShortcuts() }
                Label { text: "Updates"; font.bold: true }
                TextField { id: feed; text: player.updateFeed; placeholderText: "HTTPS release manifest"; Layout.fillWidth: true }
                Button { text: "Apply update feed"; onClicked: player.updateFeed=feed.text }
                RowLayout {
                    Button { text: "Check"; onClicked: player.updater.check() }
                    Button { text: "Install"; enabled: player.updater.ready; onClicked: player.updater.install() }
                }
                Label { text: player.updater.status; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            }
        }
    }
}
