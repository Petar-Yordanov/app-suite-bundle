import QtQuick
import QtQuick.Controls.Basic
import "../theme" as Theme

ComboBox {
    id: control
    implicitHeight: 32
    implicitWidth: 154
    leftPadding: 10
    rightPadding: 28
    font.pixelSize: Theme.Typography.bodyLg
    contentItem: Text {
        leftPadding: 1
        rightPadding: 0
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.AppTheme.text : Theme.AppTheme.disabledText
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: FIcon {
        x: control.width - width - 7
        y: (control.height - height) / 2
        iconSize: 16
        name: "expand-more"
        opacity: control.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: Theme.Metrics.radiusMd
        color: control.down ? Theme.AppTheme.pressed : control.hovered ? Theme.AppTheme.hover : Theme.AppTheme.surface3
        border.color: control.activeFocus ? Theme.AppTheme.accent : Theme.AppTheme.border
        border.width: control.activeFocus ? 2 : 1
    }
    delegate: ItemDelegate {
        id: choice
        width: control.popup.width - control.popup.leftPadding - control.popup.rightPadding
        implicitHeight: 32
        highlighted: control.highlightedIndex === index
        text: control.textAt(index)
        contentItem: Text {
            text: choice.text
            color: Theme.AppTheme.text
            font.pixelSize: Theme.Typography.bodyLg
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            elide: Text.ElideRight
        }
        background: Rectangle {
            color: choice.highlighted || choice.hovered ? Theme.AppTheme.menuHighlight : "transparent"
            radius: Theme.Metrics.radiusSm
            border.color: choice.highlighted ? Theme.AppTheme.menuHighlightBorder : "transparent"
        }
    }
    popup: Popup {
        y: control.height + 3
        width: control.width
        implicitHeight: Math.min(contentItem.implicitHeight + 10, 320)
        padding: 5
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollBar.vertical: FScrollBar { policy: ScrollBar.AsNeeded }
        }
        background: Rectangle {
            color: Theme.AppTheme.popupBg
            radius: Theme.Metrics.radiusLg
            border.color: Theme.AppTheme.border
        }
    }
}
