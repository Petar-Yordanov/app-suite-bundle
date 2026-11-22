import QtQuick
import "ui" as Ui

// Legacy compatibility: all buttons share the Findex-aligned FButton styling.
Ui.FButton {
    id: control
    property string symbol: ""
    iconName: symbol === "+" ? "add" :
              symbol === "✓" ? "check" :
              symbol === "↗" ? "launch" :
              symbol === "▣" ? "folder" :
              symbol === "⎘" ? "content-paste" : ""
}
