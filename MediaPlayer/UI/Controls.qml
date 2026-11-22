import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    padding: 8
    component CompactButton: Button { implicitWidth: Math.max(42,contentItem.implicitWidth+14); implicitHeight: 32; padding: 5 }
    function time(ms) {
        var sec = Math.max(0, Math.floor(ms / 1000))
        var h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = sec % 60
        var two = function(n) { return n < 10 ? "0" + n : "" + n }
        return (h > 0 ? h + ":" + two(m) : m) + ":" + two(s)
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 3
        RowLayout {
            Layout.fillWidth: true
            Label { text: player.title; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: player.status; elide: Text.ElideRight; Layout.maximumWidth: 240; opacity: 0.75 }
        }
        RowLayout {
            Layout.fillWidth: true
            Label { text: root.time(progress.pressed ? progress.value : player.position); font.family: "monospace" }
            Slider {
                id: progress
                from: 0; to: Math.max(1, player.duration); Layout.fillWidth: true
                enabled: player.seekable
                property real dragTarget: 0
                Binding on value { value: player.position; when: !progress.pressed }
                onPressedChanged: { if (pressed) dragTarget=value; else if (enabled) player.seek(dragTarget) }
                // Keyboard changes should seek immediately without a drag.
                onMoved: { if (pressed) dragTarget=value; else player.seek(value) }
            }
            Label { text: root.time(player.duration); font.family: "monospace" }
        }
        RowLayout {
            Layout.fillWidth: true
            spacing: 4
            CompactButton { text: "Open"; onClicked: player.openDialog() }
            CompactButton { text: "Prev"; enabled: player.hasMedia || player.playlist.length > 0; onClicked: player.previous() }
            CompactButton { text: player.busy ? "Cancel" : player.playing ? "Pause" : "Play"; onClicked: player.togglePlay() }
            CompactButton { text: "Stop"; enabled: player.hasMedia; onClicked: player.stop() }
            CompactButton { text: "Next"; enabled: player.hasMedia || player.playlist.length > 0; onClicked: player.next() }
            Item { Layout.fillWidth: true }
            CompactButton { text: player.muted ? "Unmute" : "Mute"; onClicked: player.muted = !player.muted }
            Slider { from: 0; to: 100; stepSize: 1; value: player.volume; Layout.preferredWidth: 86; onMoved: player.volume = Math.round(value) }
            Label { text: player.volume + "%"; Layout.preferredWidth: 35 }
            CompactButton { text: "List"; checkable: true; checked: player.panel === "playlist"; onClicked: player.setPanel("playlist") }
            CompactButton { text: "Tracks"; checkable: true; checked: player.panel === "tracks"; onClicked: player.setPanel("tracks") }
            CompactButton { text: player.fullscreen ? "Exit full" : "Fullscreen"; onClicked: player.toggleFullscreen() }
            CompactButton { text: "Settings"; checkable: true; checked: player.panel === "settings"; onClicked: player.setPanel("settings") }
        }
    }
}
