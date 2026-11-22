import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

ToolTip {
    id: control
    delay: 500
    timeout: 3500
    padding: 8
    contentItem: Text {
        text: control.text
        color: Theme.AppTheme.text
        font.pixelSize: Theme.Typography.caption
        font.weight: Font.DemiBold
        wrapMode: Text.WordWrap
    }
    background: Rectangle {
        radius: Theme.Metrics.radiusMd
        color: Theme.AppTheme.popupAltBg
        border.color: Theme.AppTheme.border
    }
}
