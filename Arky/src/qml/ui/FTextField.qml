import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

TextField {
    id: control
    implicitHeight: Theme.Metrics.controlHeightLg
    font.pixelSize: Theme.Typography.bodyLg
    color: Theme.AppTheme.text
    placeholderTextColor: Theme.AppTheme.muted
    selectionColor: Theme.AppTheme.selected
    selectedTextColor: Theme.AppTheme.text
    leftPadding: 10
    rightPadding: 10
    background: Rectangle {
        radius: Theme.Metrics.radiusMd
        color: Theme.AppTheme.surface3
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.AppTheme.accent : Theme.AppTheme.border
    }
}
