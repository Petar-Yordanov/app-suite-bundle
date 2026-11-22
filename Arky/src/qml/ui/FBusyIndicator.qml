import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

BusyIndicator {
    id: control
    implicitWidth: 22
    implicitHeight: 22
    contentItem: Item {
        RotationAnimator on rotation {
            running: control.visible && control.running
            loops: Animation.Infinite
            from: 0
            to: 360
            duration: 950
        }
        Repeater {
            model: 8
            Rectangle {
                width: 3
                height: 7
                radius: 1.5
                color: Theme.AppTheme.accent
                opacity: (index + 1) / 9
                x: control.width / 2 - width / 2
                y: 0
                transform: Rotation { origin.x: width / 2; origin.y: control.height / 2 - y; angle: index * 45 }
            }
        }
    }
}
