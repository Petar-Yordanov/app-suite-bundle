import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    width: 640
    height: 740
    minimumWidth: 510
    minimumHeight: 400
    visible: false
    title: "Screen Recorder"
    color: recorder.dark ? "#171a1f" : "#f6f7f9"
    readonly property color fg: recorder.dark ? "#e6e9ee" : "#202631"
    readonly property color muted: recorder.dark ? "#a8b0bc" : "#606c7c"
    readonly property color surface: recorder.dark ? "#242931" : "#ffffff"
    readonly property bool idle: recorder.state === 0
    readonly property bool paused: recorder.state === 4
    readonly property bool recording: recorder.state === 2
    palette.window: root.color
    palette.windowText: fg
    palette.text: fg
    palette.base: surface
    palette.button: surface
    palette.buttonText: fg
    palette.highlight: "#4c81cf"
    palette.highlightedText: "#ffffff"
    onClosing: function(close) { close.accepted = false; recorder.closeUi() }

    FolderDialog {
        id: folder
        title: "Choose recordings folder"
        onAccepted: recorder.setOutputUrl(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 12
        RowLayout {
            Layout.fillWidth: true
            Rectangle {
                width: 15; height: 15; radius: 7.5
                color: root.idle ? "#7e8c9e" : root.paused ? "#d3a650" : "#de4848"
            }
            Label { text: root.idle ? "Screen recorder" : root.paused ? "Paused" : "Recording"; font.pixelSize: 21; font.weight: Font.DemiBold; color: root.fg }
            Item { Layout.fillWidth: true }
            Label { text: recorder.duration; font.pixelSize: 21; font.family: "monospace"; color: root.fg }
        }

        ScrollView {
            id: scroll
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: scroll.availableWidth
                spacing: 12
                GroupBox {
                    title: "Capture"
                    Layout.fillWidth: true
                    enabled: root.idle
                    ColumnLayout {
                        anchors.fill: parent
                        RowLayout {
                            Layout.fillWidth: true
                            ComboBox {
                                id: mode
                                model: ["Entire screen", "Select region", "Application window"]
                                Layout.fillWidth: true
                                onActivated: source.currentIndex = 0
                            }
                            Button { text: "Refresh"; enabled: !recorder.discovering; onClicked: recorder.refresh() }
                        }
                        ComboBox {
                            id: source
                            Layout.fillWidth: true
                            model: mode.currentIndex === 2 ? recorder.windows : recorder.screens
                            textRole: "name"
                        }
                        CheckBox { text: "Include mouse cursor"; checked: recorder.settings.cursor; onToggled: recorder.setSetting("cursor", checked) }
                    }
                }
                GroupBox {
                    title: "Video"
                    Layout.fillWidth: true
                    enabled: root.idle
                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        Label { text: "Resolution" }
                        ComboBox {
                            Layout.fillWidth: true
                            model: ["Native (no upscale)", "720p max", "1080p max", "1440p max", "2160p max"]
                            currentIndex: [0,720,1080,1440,2160].indexOf(recorder.settings.height)
                            onActivated: recorder.setSetting("height", [0,720,1080,1440,2160][currentIndex])
                        }
                        Label { text: "Frames per second" }
                        SpinBox { from: 10; to: 120; stepSize: 5; editable: true; value: recorder.settings.fps; onValueModified: recorder.setSetting("fps",value) }
                        Label { text: "Bitrate (kbps)" }
                        SpinBox { from: 500; to: 100000; stepSize: 500; editable: true; value: recorder.settings.bitrate; onValueModified: recorder.setSetting("bitrate",value) }
                        Label { text: "Encoding quality" }
                        ComboBox { Layout.fillWidth: true; model: ["Fast", "Balanced", "High"]; currentIndex: recorder.settings.quality; onActivated: recorder.setSetting("quality",currentIndex) }
                        Label { text: "Encoder" }
                        ComboBox { Layout.fillWidth: true; model: ["Prefer hardware automatically", "Software only (H.264)"]; currentIndex: recorder.settings.hardware ? 0 : 1; onActivated: recorder.setSetting("hardware",currentIndex===0) }
                    }
                }
                GroupBox {
                    title: "Audio"
                    Layout.fillWidth: true
                    enabled: root.idle
                    ColumnLayout {
                        anchors.fill: parent
                        CheckBox { text: "Record microphone"; checked: recorder.settings.micEnabled; onToggled: recorder.setSetting("micEnabled",checked) }
                        ComboBox {
                            id: mic
                            Layout.fillWidth: true
                            enabled: recorder.settings.micEnabled && !recorder.discovering
                            textRole: "name"; valueRole: "id"; model: recorder.microphones
                            currentIndex: indexOfValue(recorder.settings.mic)
                            onActivated: recorder.setSetting("mic",currentValue)
                        }
                        CheckBox { text: "Record system audio"; checked: recorder.settings.systemAudio; enabled: recorder.systemAvailable || checked; onToggled: recorder.setSetting("systemAudio",checked) }
                        Label { visible: recorder.discovering; text: "Discovering audio devices…"; color: root.muted }
                        Label { visible: !recorder.systemAvailable && !recorder.discovering; text: "No system audio source available."; color: root.muted }
                    }
                }
                GroupBox {
                    title: "Settings"
                    Layout.fillWidth: true
                    enabled: root.idle
                    GridLayout {
                        anchors.fill: parent
                        columns: 2
                        Label { text: "Output directory" }
                        RowLayout {
                            Layout.fillWidth: true
                            TextField { Layout.fillWidth: true; text: recorder.settings.output; onEditingFinished: recorder.setSetting("output",text) }
                            Button { text: "Browse"; onClicked: folder.open() }
                        }
                        Label { text: "Theme" }
                        ComboBox { model: ["System","Light","Dark"]; currentIndex: model.indexOf(recorder.settings.theme); onActivated: recorder.setSetting("theme",currentText) }
                        Label { text: "Open / stop shortcut" }
                        TextField { Layout.fillWidth: true; text: recorder.settings.shortcut; onEditingFinished: recorder.setSetting("shortcut",text) }
                        Label { text: "Pause / resume shortcut" }
                        TextField { Layout.fillWidth: true; text: recorder.settings.pauseShortcut; onEditingFinished: recorder.setSetting("pauseShortcut",text) }
                        Label { Layout.columnSpan: 2; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: recorder.hotkeyStatus; color: root.muted }
                        CheckBox { Layout.columnSpan: 2; text: "Start in background at login"; checked: recorder.settings.autostart; onToggled: recorder.setSetting("autostart",checked) }
                        Label { text: "Update feed (optional)" }
                        TextField { Layout.fillWidth: true; text: recorder.settings.updateUrl; placeholderText: "https://…/release.json"; onEditingFinished: recorder.setSetting("updateUrl",text) }
                        Label { Layout.columnSpan: 2; Layout.fillWidth: true; text: updater.status; wrapMode: Text.WordWrap; color: root.muted }
                        RowLayout {
                            Layout.columnSpan: 2
                            Button { text: "Check for updates"; onClicked: updater.check() }
                            Button { text: "Install update"; enabled: updater.ready; onClicked: updater.install() }
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: recorder.platformLimits
                    color: root.muted
                    wrapMode: Text.WordWrap
                    font.pixelSize: 12
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: recorder.status
            wrapMode: Text.WrapAnywhere
            maximumLineCount: 4
            elide: Text.ElideRight
            color: root.muted
        }
        RowLayout {
            Layout.fillWidth: true
            Button { text: "Record"; visible: root.idle; enabled: source.currentIndex>=0; highlighted: true; onClicked: recorder.begin(mode.currentIndex,source.currentIndex) }
            Button { text: root.paused ? "Resume" : "Pause"; visible: !root.idle; enabled: root.paused || root.recording; onClicked: recorder.pauseResume() }
            Button { text: "Stop"; visible: !root.idle; enabled: recorder.state===1||recorder.state===2||recorder.state===3||recorder.state===4; highlighted: true; onClicked: recorder.stop() }
            Button { text: "Recordings"; onClicked: recorder.openOutput() }
            Button { text: "Play last"; enabled: recorder.lastFile.length>0; onClicked: recorder.openLast() }
            Item { Layout.fillWidth: true }
            Button { text: "Hide"; onClicked: recorder.closeUi() }
            Button { text: "Quit"; onClicked: recorder.quit() }
        }
    }
}
