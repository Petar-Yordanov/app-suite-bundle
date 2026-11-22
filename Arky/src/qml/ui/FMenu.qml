import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

Popup {
    id: control
    property int menuWidth: 208
    default property alias contentData: contentColumn.data
    modal: false
    focus: true
    padding: 7
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    width: menuWidth + 14
    implicitHeight: contentColumn.childrenRect.height + 14
    background: Rectangle { radius: Theme.Metrics.radiusLg; color: Theme.AppTheme.popupBg; border.color: Theme.AppTheme.border }
    contentItem: Column {
        id: contentColumn
        property var menuOwner: control
        width: control.menuWidth
        spacing: 0
    }
    // Position relative to the actual trigger, not guessed pixel coordinates.
    // This survives UI scaling, localization and future menu layout changes.
    function popupBelow(item) {
        if (!item || !parent) return
        var point = item.mapToItem(parent, 0, item.height + 2)
        popupAt(point.x, point.y)
    }
    function popupAt(px, py) {
        control.x = Math.max(4, Math.min(px, (parent ? parent.width : 10000) - width - 4))
        control.y = Math.max(4, Math.min(py, (parent ? parent.height : 10000) - implicitHeight - 4))
        open()
    }
}
