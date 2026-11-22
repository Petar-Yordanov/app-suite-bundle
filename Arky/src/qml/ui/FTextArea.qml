import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

TextArea {
    id: control
    font.pixelSize: Theme.Typography.bodyLg
    color: Theme.AppTheme.text
    selectionColor: Theme.AppTheme.selected
    selectedTextColor: Theme.AppTheme.text
    background: Rectangle { color: Theme.AppTheme.surface3; radius: 6; border.color: Theme.AppTheme.border }
}
