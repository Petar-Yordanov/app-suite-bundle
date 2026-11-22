import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: root
    color: "transparent"
    visible: false
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool
    property real x0: 0
    property real y0: 0
    property real x1: 0
    property real y1: 0
    readonly property real sx: Math.max(0,Math.min(x0,x1))
    readonly property real sy: Math.max(0,Math.min(y0,y1))
    readonly property real sw: Math.max(0,Math.min(width,Math.max(x0,x1))-sx)
    readonly property real sh: Math.max(0,Math.min(height,Math.max(y0,y1))-sy)
    Rectangle { width: root.width; height: root.sy; color: "#99000000" }
    Rectangle { y: root.sy+root.sh; width: root.width; height: Math.max(0,root.height-y); color: "#99000000" }
    Rectangle { y: root.sy; width: root.sx; height: root.sh; color: "#99000000" }
    Rectangle { x: root.sx+root.sw; y: root.sy; width: Math.max(0,root.width-x); height: root.sh; color: "#99000000" }
    Rectangle { x: root.sx; y: root.sy; width: root.sw; height: root.sh; color: "transparent"; border.color: "#80baff"; border.width: 2; visible: mouse.pressed }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: 20; radius: 5; color: "#dd20242c"
        width: hint.implicitWidth+24; height: hint.implicitHeight+14
        visible: !mouse.pressed
        Text { id: hint; anchors.centerIn: parent; color: "white"; text: "Drag to select recording region  |  Esc to cancel"; font.pixelSize: 16 }
    }
    MouseArea {
        id: mouse
        anchors.fill: parent
        cursorShape: Qt.CrossCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(m) {
            if(m.button===Qt.RightButton){recorder.cancelRegion();return}
            root.x0=m.x;root.y0=m.y;root.x1=m.x;root.y1=m.y
        }
        onPositionChanged: function(m) { if(pressed){root.x1=m.x;root.y1=m.y} }
        onReleased: function(m) {
            if(m.button!==Qt.LeftButton)return
            root.x1=m.x;root.y1=m.y
            recorder.finishRegion(root.sx,root.sy,root.sw,root.sh,root.width,root.height)
        }
    }
    Shortcut { sequence: "Escape"; onActivated: recorder.cancelRegion() }
    onClosing: function(close) { close.accepted=false; recorder.cancelRegion() }
}
