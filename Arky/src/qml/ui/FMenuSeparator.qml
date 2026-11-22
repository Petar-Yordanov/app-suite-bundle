import QtQuick
import "../theme" as Theme

Item {
    width: parent ? parent.width : 208
    height: 11
    Rectangle { anchors.centerIn: parent; width: parent.width - 16; height: 1; color: Theme.AppTheme.separator }
}
