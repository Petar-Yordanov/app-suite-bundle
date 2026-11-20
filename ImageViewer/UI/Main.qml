import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    visible: true
    width: 1100; height: 760
    minimumWidth: 540; minimumHeight: 360
    title: viewer.fileName.length ? viewer.fileName + " — Image Viewer" : "Image Viewer"
    readonly property bool dark: viewer.theme === "dark" || (viewer.theme === "system" && viewer.systemDark)
    readonly property color chrome: dark ? "#202124" : "#f7f8fa"
    readonly property color canvas: dark ? "#151619" : "#e9ebef"
    readonly property color line: dark ? "#3a3b40" : "#d4d8df"
    readonly property color ink: dark ? "#eeeeef" : "#20242a"
    readonly property color secondary: dark ? "#b0b3bb" : "#626974"
    readonly property bool imageReady: picture.status === Image.Ready && viewer.imageUrl.length > 0
    property bool showInfo: false
    property real zoom: 1
    property real panX: 0
    property real panY: 0
    property int angle: 0
    property bool fitting: true
    readonly property bool turned: Math.abs(angle % 180) === 90
    readonly property real rotatedWidth: turned ? viewer.imageHeight : viewer.imageWidth
    readonly property real rotatedHeight: turned ? viewer.imageWidth : viewer.imageHeight
    readonly property real fitScale: rotatedWidth > 0 && rotatedHeight > 0 ? Math.min(1, Math.max(0.001, Math.min((viewport.width - 24) / rotatedWidth, (viewport.height - 24) / rotatedHeight))) : 1
    readonly property real scaleFactor: fitting ? fitScale : zoom
    color: chrome
    palette.window: chrome
    palette.base: dark ? "#282a2e" : "#ffffff"
    palette.button: dark ? "#303237" : "#eef0f4"
    palette.windowText: ink; palette.text: ink; palette.buttonText: ink
    palette.highlight: dark ? "#578ce7" : "#2866c7"
    palette.highlightedText: "white"
    onVisibilityChanged: viewer.setWindowVisible(visible && visibility !== Window.Minimized)

    function fit() { fitting = true; panX = 0; panY = 0 }
    function actual() { fitting = false; zoom = 1; panX = 0; panY = 0 }
    function rotate(delta) { angle = (angle + delta) % 360; panX = 0; panY = 0 }
    function zoomBy(factor, x, y) {
        if (viewer.imageWidth <= 0) return
        let old = scaleFactor
        let next = Math.max(0.01, Math.min(32, old * factor))
        let dx = x - viewport.width / 2, dy = y - viewport.height / 2
        panX = dx - (dx - panX) * next / old
        panY = dy - (dy - panY) * next / old
        fitting = false; zoom = next; clampPan()
    }
    function clampPan() {
        let limitX = Math.max(0, (rotatedWidth * scaleFactor - viewport.width) / 2)
        let limitY = Math.max(0, (rotatedHeight * scaleFactor - viewport.height) / 2)
        panX = Math.max(-limitX, Math.min(limitX, panX))
        panY = Math.max(-limitY, Math.min(limitY, panY))
    }
    function fullscreen() { if (visibility === Window.FullScreen) showNormal(); else showFullScreen() }
    function trash() { if (viewer.path.length && !viewer.busy) { trashDialog.expectedPath = viewer.path; trashDialog.expectedName = viewer.fileName; trashDialog.open() } }
    Connections { target: viewer; function onPathChanged() { win.angle = 0; win.fit() } }
    onWidthChanged: clampPan()
    onHeightChanged: clampPan()

    FileDialog { id: chooser; title: "Open image"; fileMode: FileDialog.OpenFile; nameFilters: viewer.nameFilters; onAccepted: viewer.open(selectedFile) }
    Dialog {
        id: trashDialog; parent: Overlay.overlay; anchors.centerIn: parent
        property string expectedPath: ""
        property string expectedName: ""
        title: "Move image to trash?"; modal: true; standardButtons: Dialog.Yes | Dialog.Cancel
        width: Math.min(win.width - 40, 420)
        contentItem: Label { text: trashDialog.expectedName + " will be moved to the system trash/recycle bin."; wrapMode: Text.Wrap }
        onAccepted: viewer.moveToTrash(expectedPath)
    }
    Dialog {
        id: settings; parent: Overlay.overlay; anchors.centerIn: parent
        modal: true; title: "Settings"; width: Math.min(560, win.width - 32); height: Math.min(600, win.height - 32)
        standardButtons: Dialog.Close
        contentItem: ScrollView {
            clip: true; contentWidth: availableWidth
            ColumnLayout {
                width: settings.availableWidth - 16; spacing: 12
                Label { text: "Appearance"; font.bold: true }
                ComboBox {
                    Layout.fillWidth: true; model: ["System", "Light", "Dark"]
                    currentIndex: viewer.theme === "light" ? 1 : viewer.theme === "dark" ? 2 : 0
                    onActivated: viewer.theme = ["system", "light", "dark"][currentIndex]
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                RowLayout {
                    Label { text: "Keyboard shortcuts"; font.bold: true; Layout.fillWidth: true }
                    Button { text: "Reset"; onClicked: viewer.resetShortcuts() }
                }
                Label { Layout.fillWidth: true; text: "Use portable key names such as Ctrl+O, Left or F11. Empty disables a shortcut."; color: win.secondary; wrapMode: Text.Wrap }
                Repeater {
                    model: [{key:"open",name:"Open image"},{key:"previous",name:"Previous"},{key:"next",name:"Next"},{key:"zoomIn",name:"Zoom in"},{key:"zoomOut",name:"Zoom out"},{key:"fit",name:"Fit to window"},{key:"actual",name:"Actual size"},{key:"rotateRight",name:"Rotate clockwise"},{key:"rotateLeft",name:"Rotate counterclockwise"},{key:"trash",name:"Move to trash"},{key:"info",name:"Metadata"},{key:"fullscreen",name:"Fullscreen"},{key:"play",name:"Play/pause animation"},{key:"reload",name:"Reload"}]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label { text: modelData.name; Layout.fillWidth: true }
                        TextField {
                            id: shortcutEdit; Layout.preferredWidth: 145; text: viewer.shortcuts[modelData.key]; selectByMouse: true
                            onEditingFinished: { viewer.setShortcut(modelData.key, text); text = viewer.shortcuts[modelData.key] }
                            Connections { target: viewer; function onPreferencesChanged() { if (!shortcutEdit.activeFocus) shortcutEdit.text = viewer.shortcuts[modelData.key] } }
                        }
                    }
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                Label { text: "System integration"; font.bold: true }
                Button { text: "Choose default image viewer"; onClicked: viewer.defaultAppSettings() }
                Rectangle { Layout.fillWidth: true; height: 1; color: win.line }
                Label { text: "Automatic updates"; font.bold: true }
                Label { Layout.fillWidth: true; text: "Optional trusted HTTPS release manifest. No account or telemetry."; color: win.secondary; wrapMode: Text.Wrap }
                RowLayout {
                    Layout.fillWidth: true
                    TextField { id: feed; Layout.fillWidth: true; text: viewer.updateFeed; placeholderText: "https://…/release.json"; selectByMouse: true }
                    Button { text: "Save"; onClicked: viewer.setUpdateFeed(feed.text) }
                }
                Label { Layout.fillWidth: true; text: viewer.updater.status; wrapMode: Text.Wrap }
                RowLayout {
                    Button { text: "Check now"; onClicked: viewer.updater.check() }
                    Button { text: "Install update"; enabled: viewer.updater.ready; onClicked: viewer.updater.install() }
                }
            }
        }
    }
    component CommandButton: ToolButton {
        font.pixelSize: 15
        hoverEnabled: true
        ToolTip.visible: hovered && ToolTip.text.length > 0
        ToolTip.delay: 450
    }
    header: ToolBar {
        background: Rectangle { color: win.chrome; border.color: win.line; border.width: 1 }
        height: 48
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; spacing: 3
            CommandButton { text: "Open"; onClicked: chooser.open(); ToolTip.text: "Open image" }
            CommandButton { text: "‹"; font.pixelSize: 25; enabled: viewer.hasPrevious; onClicked: viewer.previous(); ToolTip.text: "Previous image" }
            CommandButton { text: "›"; font.pixelSize: 25; enabled: viewer.hasNext; onClicked: viewer.next(); ToolTip.text: "Next image" }
            Label { text: viewer.count ? (viewer.index + 1) + " / " + viewer.count : ""; color: win.secondary; visible: win.width > 680 }
            Item { Layout.fillWidth: true }
            CommandButton { text: "−"; enabled: viewer.imageWidth > 0; onClicked: win.zoomBy(1/1.25, viewport.width/2, viewport.height/2); ToolTip.text: "Zoom out" }
            Label { text: Math.round(win.scaleFactor * 100) + "%"; color: win.secondary; Layout.preferredWidth: 48; horizontalAlignment: Text.AlignHCenter }
            CommandButton { text: "+"; enabled: viewer.imageWidth > 0; onClicked: win.zoomBy(1.25, viewport.width/2, viewport.height/2); ToolTip.text: "Zoom in" }
            CommandButton { text: "Fit"; enabled: viewer.imageWidth > 0; onClicked: win.fit(); ToolTip.text: "Fit to window" }
            CommandButton { text: "1:1"; enabled: viewer.imageWidth > 0; onClicked: win.actual(); ToolTip.text: "Actual decoded size"; visible: win.width > 660 }
            CommandButton { text: "↻"; enabled: viewer.imageWidth > 0; onClicked: win.rotate(90); ToolTip.text: "Rotate clockwise" }
            CommandButton { text: viewer.playing ? "Ⅱ" : "▶"; visible: viewer.animated; onClicked: viewer.togglePlayback(); ToolTip.text: "Pause or resume animation" }
            CommandButton { text: "ⓘ"; checkable: true; checked: win.showInfo; onClicked: win.showInfo = checked; ToolTip.text: "Image metadata" }
            CommandButton { text: "⛶"; onClicked: win.fullscreen(); ToolTip.text: "Fullscreen" }
            CommandButton { text: "⋯"; ToolTip.text: "More actions"; onClicked: more.open()
                Menu { id: more; y: parent.height
                    MenuItem { text: "Reveal folder"; enabled: viewer.path.length > 0; onTriggered: viewer.reveal() }
                    MenuItem { text: "Rotate counterclockwise"; enabled: viewer.imageWidth > 0; onTriggered: win.rotate(-90) }
                    MenuItem { text: "Actual size"; enabled: viewer.imageWidth > 0; onTriggered: win.actual() }
                    MenuItem { text: "Reload"; enabled: viewer.path.length > 0; onTriggered: viewer.reload() }
                    MenuSeparator {}
                    MenuItem { text: "Move to trash"; enabled: viewer.path.length > 0 && !viewer.busy; onTriggered: win.trash() }
                    MenuItem { text: "Settings"; onTriggered: settings.open() }
                }
            }
        }
    }
    RowLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            id: viewport; Layout.fillWidth: true; Layout.fillHeight: true; color: win.canvas; clip: true
            onWidthChanged: win.clampPan()
            onHeightChanged: win.clampPan()
            Image {
                id: picture
                source: viewer.imageUrl; cache: false; asynchronous: false; smooth: true; mipmap: false
                width: viewer.imageWidth * win.scaleFactor; height: viewer.imageHeight * win.scaleFactor
                x: (viewport.width - width) / 2 + win.panX; y: (viewport.height - height) / 2 + win.panY
                rotation: win.angle; transformOrigin: Item.Center; visible: status === Image.Ready
                fillMode: Image.Stretch
            }
            Column {
                anchors.centerIn: parent; width: Math.min(350, parent.width - 40); spacing: 12
                visible: !win.imageReady
                BusyIndicator { anchors.horizontalCenter: parent.horizontalCenter; running: viewer.busy; visible: running; width: 40; height: 40 }
                Label { anchors.horizontalCenter: parent.horizontalCenter; text: viewer.busy ? "Loading image…" : viewer.path.length ? "Unable to display image" : "Open an image"; font.pixelSize: 19; color: win.ink }
                Label { width: parent.width; horizontalAlignment: Text.AlignHCenter; wrapMode: Text.Wrap; color: win.secondary; text: viewer.path.length && !viewer.busy ? viewer.status : viewer.busy ? "" : "Drop a file here, or press Ctrl+O." }
                Button { anchors.horizontalCenter: parent.horizontalCenter; text: "Browse images"; visible: !viewer.busy; onClicked: chooser.open() }
            }
            MouseArea {
                anchors.fill: parent; enabled: win.imageReady
                property real lastX; property real lastY
                acceptedButtons: Qt.LeftButton
                cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y }
                onPositionChanged: function(mouse) { if (pressed) { win.panX += mouse.x - lastX; win.panY += mouse.y - lastY; lastX = mouse.x; lastY = mouse.y; win.clampPan() } }
                onDoubleClicked: win.fullscreen()
                onWheel: function(wheel) { let d = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y; if (d !== 0) win.zoomBy(Math.pow(1.2, d / 120), wheel.x, wheel.y); wheel.accepted = true }
            }
            DropArea { anchors.fill: parent; onDropped: function(drop) { if (drop.hasUrls && drop.urls.length) { viewer.open(drop.urls[0]); drop.acceptProposedAction() } } }
        }
        Rectangle {
            visible: win.showInfo; Layout.preferredWidth: Math.min(310, win.width * 0.36); Layout.fillHeight: true; color: win.chrome; border.color: win.line
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 14; spacing: 12
                RowLayout {
                    Label { text: "Image information"; font.bold: true; Layout.fillWidth: true }
                    ToolButton { text: "×"; onClicked: win.showInfo = false }
                }
                Label { Layout.fillWidth: true; text: viewer.fileName; wrapMode: Text.WrapAnywhere; font.bold: true }
                Label { Layout.fillWidth: true; text: viewer.path; wrapMode: Text.WrapAnywhere; color: win.secondary; font.pixelSize: 12 }
                ListView {
                    Layout.fillWidth: true; Layout.fillHeight: true; clip: true; spacing: 12; model: viewer.metadata
                    delegate: Column {
                        required property var modelData; width: ListView.view.width; spacing: 3
                        Label { text: modelData.name; color: win.secondary; font.pixelSize: 12; width: parent.width; wrapMode: Text.Wrap }
                        Label { text: modelData.value; color: win.ink; width: parent.width; wrapMode: Text.WrapAnywhere; elide: Text.ElideNone }
                    }
                    ScrollBar.vertical: ScrollBar {}
                }
            }
        }
    }
    footer: Rectangle {
        color: win.chrome; height: 30; border.color: win.line
        RowLayout { anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            Label { text: viewer.fileName; elide: Text.ElideMiddle; Layout.fillWidth: true; color: win.ink; font.pixelSize: 12 }
            Label { text: viewer.status; elide: Text.ElideRight; Layout.maximumWidth: Math.max(180, win.width * 0.65); color: win.secondary; font.pixelSize: 12 }
        }
    }
    readonly property bool shortcutsEnabled: !settings.visible && !trashDialog.visible
    Shortcut { sequence: viewer.shortcuts.open; enabled: win.shortcutsEnabled; onActivated: chooser.open() }
    Shortcut { sequence: viewer.shortcuts.previous; enabled: win.shortcutsEnabled; onActivated: viewer.previous() }
    Shortcut { sequence: viewer.shortcuts.next; enabled: win.shortcutsEnabled; onActivated: viewer.next() }
    Shortcut { sequence: viewer.shortcuts.zoomIn; enabled: win.shortcutsEnabled; onActivated: win.zoomBy(1.25, viewport.width/2, viewport.height/2) }
    Shortcut { sequence: viewer.shortcuts.zoomOut; enabled: win.shortcutsEnabled; onActivated: win.zoomBy(1/1.25, viewport.width/2, viewport.height/2) }
    Shortcut { sequence: viewer.shortcuts.fit; enabled: win.shortcutsEnabled; onActivated: win.fit() }
    Shortcut { sequence: viewer.shortcuts.actual; enabled: win.shortcutsEnabled; onActivated: win.actual() }
    Shortcut { sequence: viewer.shortcuts.rotateRight; enabled: win.shortcutsEnabled; onActivated: win.rotate(90) }
    Shortcut { sequence: viewer.shortcuts.rotateLeft; enabled: win.shortcutsEnabled; onActivated: win.rotate(-90) }
    Shortcut { sequence: viewer.shortcuts.trash; enabled: win.shortcutsEnabled; onActivated: win.trash() }
    Shortcut { sequence: viewer.shortcuts.info; enabled: win.shortcutsEnabled; onActivated: win.showInfo = !win.showInfo }
    Shortcut { sequence: viewer.shortcuts.fullscreen; enabled: win.shortcutsEnabled; onActivated: win.fullscreen() }
    Shortcut { sequence: viewer.shortcuts.play; enabled: win.shortcutsEnabled; onActivated: viewer.togglePlayback() }
    Shortcut { sequence: viewer.shortcuts.reload; enabled: win.shortcutsEnabled; onActivated: viewer.reload() }
    Shortcut { sequence: "Escape"; enabled: win.shortcutsEnabled && win.visibility === Window.FullScreen; onActivated: win.showNormal() }
}
