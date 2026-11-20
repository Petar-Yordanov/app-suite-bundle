import QtQuick
import QtQuick.Controls
import QtQuick.Window

Window {
    id: root
    visible: false
    flags: Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    color: "black"
    onClosing: shot.uiClosed()
    property real startX: 0
    property real startY: 0
    property real endX: 0
    property real endY: 0
    property real sx: Math.max(0,Math.min(startX,endX))
    property real sy: Math.max(0,Math.min(startY,endY))
    property real sw: Math.min(width,Math.max(startX,endX))-sx
    property real sh: Math.min(height,Math.max(startY,endY))-sy
    Image { anchors.fill: parent; source: "image://capture/current"; cache: false; fillMode: Image.Stretch; smooth: false }
    Rectangle { x: 0; y: 0; width: root.width; height: root.sy; color: "#88000000" }
    Rectangle { x: 0; y: root.sy+root.sh; width: root.width; height: Math.max(0,root.height-y); color: "#88000000" }
    Rectangle { x: 0; y: root.sy; width: root.sx; height: root.sh; color: "#88000000" }
    Rectangle { x: root.sx+root.sw; y: root.sy; width: Math.max(0,root.width-x); height: root.sh; color: "#88000000" }
    Rectangle { x: root.sx; y: root.sy; width: root.sw; height: root.sh; color: "transparent"; border.color: "#80baff"; border.width: 2; visible: selector.pressed }
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter; y: 22; width: hint.implicitWidth+24; height: hint.implicitHeight+14
        color: "#dd222222"; radius: 5; visible: !selector.pressed
        Text { id: hint; anchors.centerIn: parent; text: "Drag to select a region  |  Esc to cancel"; color: "white"; font.pixelSize: 15 }
    }
    MouseArea {
        id: selector
        anchors.fill: parent; cursorShape: Qt.CrossCursor; acceptedButtons: Qt.LeftButton | Qt.RightButton
        onPressed: function(m) {
            if (m.button === Qt.RightButton) { shot.cancel(); return }
            root.startX=m.x; root.startY=m.y; root.endX=m.x; root.endY=m.y
        }
        onPositionChanged: function(m) { if (pressed) { root.endX=m.x; root.endY=m.y } }
        onReleased: function(m) {
            if (m.button === Qt.LeftButton) {
                root.endX=m.x; root.endY=m.y
                if (root.sw >= 1 && root.sh >= 1) shot.finishRegion(root.sx,root.sy,root.sw,root.sh,root.width,root.height)
            }
        }
    }
    Shortcut { sequence: "Escape"; onActivated: shot.cancel() }
}
