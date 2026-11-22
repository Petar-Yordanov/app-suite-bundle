import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: root
    width: 1000
    height: 680
    title: "Choose region on shared source"
    visible: false
    color: "#242931"
    property real x0: 0
    property real y0: 0
    property real x1: 0
    property real y1: 0
    Label {
        id: hint
        anchors.top: parent.top
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.topMargin: 12
        text: "Drag over the preview to select a recording region. Esc cancels."
        color: "white"
    }
    Image {
        id: preview
        anchors.top: hint.bottom
        anchors.bottom: parent.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.margins: 12
        source: "image://portal/selected"
        cache: false
        fillMode: Image.PreserveAspectFit
        smooth: true
        Item {
            id: area
            anchors.centerIn: parent
            width: preview.paintedWidth
            height: preview.paintedHeight
            clip: true
            readonly property real sx: Math.min(root.x0,root.x1)
            readonly property real sy: Math.min(root.y0,root.y1)
            readonly property real sw: Math.abs(root.x1-root.x0)
            readonly property real sh: Math.abs(root.y1-root.y0)
            Rectangle { x: area.sx; y: area.sy; width: area.sw; height: area.sh; color: "#336bafff"; border.width: 2; border.color: "#80baff"; visible: mouse.pressed }
            MouseArea {
                id: mouse
                anchors.fill: parent
                cursorShape: Qt.CrossCursor
                onPressed: function(m) {root.x0=m.x;root.y0=m.y;root.x1=m.x;root.y1=m.y}
                onPositionChanged: function(m) {if(pressed){root.x1=Math.max(0,Math.min(width,m.x));root.y1=Math.max(0,Math.min(height,m.y))}}
                onReleased: function(m) {recorder.finishRegion(area.sx,area.sy,area.sw,area.sh,area.width,area.height)}
            }
        }
    }
    Shortcut { sequence: "Escape"; onActivated: recorder.cancelRegion() }
    onClosing: function(close) {close.accepted=false;recorder.cancelRegion()}
}
