import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import QtQuick.Dialogs
import AppSuite.Terminal 1.0

ApplicationWindow {
    id: window
    width: 1150; height: 720
    minimumWidth: 610; minimumHeight: 420
    visible: true
    title: "Terminal Emulator" + (controller.active ? " - " + controller.active.title : "")
    property bool dark: controller.options.theme === "Dark" || (controller.options.theme === "System" && Qt.styleHints.colorScheme === Qt.Dark)
    property color panelColor: dark ? "#21242a" : "#f1f3f6"
    property color lineColor: dark ? "#373c46" : "#cdd2da"
    property color mutedColor: dark ? "#a6b0c0" : "#667180"
    property var activeView: null
    color: "transparent"
    background: Rectangle {color: "transparent"}
    palette.window: panelColor
    palette.base: dark ? "#1a1d22" : "#ffffff"
    palette.alternateBase: panelColor
    palette.button: panelColor
    palette.text: dark ? "#e3e8ef" : "#232831"
    palette.buttonText: palette.text
    palette.windowText: palette.text
    palette.highlight: dark ? "#345c96" : "#c6dcff"
    palette.highlightedText: dark ? "#ffffff" : "#152b4c"
    function newTab() {controller.newTab("")}
    function rename() {
        if (controller.currentIndex < 0) return
        renameField.text = controller.tabs[controller.currentIndex].title
        renameDialog.open()
    }
    function focusTerminal() {if (activeView) activeView.forceActiveFocus()}
    function toggleFind() {
        searchPanel.visible = !searchPanel.visible
        if (searchPanel.visible) {searchField.forceActiveFocus(); searchField.selectAll()}
        else {if (controller.active) controller.active.clearFind(); focusTerminal()}
    }
    function search(backwards) {
        if (controller.active)
            searchResult.text = controller.active.find(searchField.text, backwards) ? "Match" : "No match"
    }
    Shortcut {sequence: controller.options.newTabKey; onActivated: window.newTab()}
    Shortcut {sequence: controller.options.closePaneKey; onActivated: controller.closePane()}
    Shortcut {sequence: controller.options.splitHKey; onActivated: controller.split(true, "")}
    Shortcut {sequence: controller.options.splitVKey; onActivated: controller.split(false, "")}
    Shortcut {sequence: controller.options.copyKey; onActivated: if (window.activeView) window.activeView.copy()}
    Shortcut {sequence: controller.options.pasteKey; onActivated: if (window.activeView) window.activeView.paste()}
    Shortcut {sequence: controller.options.findKey; onActivated: window.toggleFind()}
    Shortcut {sequence: controller.options.nextTabKey; onActivated: controller.cycleTab(1)}
    Shortcut {sequence: controller.options.prevTabKey; onActivated: controller.cycleTab(-1)}
    Shortcut {sequence: controller.options.settingsKey; onActivated: settingsDialog.open()}
    Shortcut {sequence: controller.options.renameKey; onActivated: window.rename()}
    Shortcut {sequence: controller.options.duplicateKey; onActivated: controller.duplicateTab(controller.currentIndex)}
    header: Rectangle {
        implicitHeight: 77; color: window.panelColor
        ColumnLayout {
            anchors.fill: parent; spacing: 0
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 37
                Layout.leftMargin: 6; Layout.rightMargin: 6; spacing: 4
                Flickable {
                    id: tabScroll
                    Layout.fillWidth: true; Layout.fillHeight: true
                    clip: true; contentWidth: tabRow.width; contentHeight: height
                    flickableDirection: Flickable.HorizontalFlick
                    Row {
                        id: tabRow; height: parent.height; spacing: 3
                        Repeater {
                            model: controller.tabs
                            delegate: Rectangle {
                                required property int index
                                required property var modelData
                                height: tabRow.height-2; width: 145; radius: 4
                                color: index === controller.currentIndex ? window.palette.highlight : window.palette.base
                                RowLayout {
                                    anchors.fill: parent; spacing: 0
                                    ToolButton {
                                        id: tabButton
                                        Layout.fillWidth: true
                                        text: modelData.title
                                        contentItem: Label {text: tabButton.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter}
                                        onClicked: controller.selectTab(index)
                                        onDoubleClicked: {controller.selectTab(index); window.rename()}
                                    }
                                    ToolButton {text: "×"; onClicked: controller.closeTab(index)}
                                }
                            }
                        }
                    }
                }
                ToolButton {text: "+"; onClicked: window.newTab()}
                ComboBox {
                    id: shellPicker
                    Layout.preferredWidth: 168
                    model: controller.shells; textRole: "name"; valueRole: "id"
                    displayText: "Shell: " + currentText
                    onActivated: controller.newTab(currentValue)
                }
            }
            RowLayout {
                Layout.fillWidth: true; Layout.preferredHeight: 38
                Layout.leftMargin: 6; Layout.rightMargin: 6; spacing: 2
                ToolButton {text: "Split ↔"; onClicked: controller.split(true, "")}
                ToolButton {text: "Split ↕"; onClicked: controller.split(false, "")}
                ToolButton {text: "Copy"; enabled: !!window.activeView && window.activeView.hasSelection; onClicked: window.activeView.copy()}
                ToolButton {text: "Paste"; enabled: !!window.activeView; onClicked: window.activeView.paste()}
                ToolButton {text: "Find"; onClicked: window.toggleFind()}
                ToolButton {text: "Rename"; onClicked: window.rename()}
                ToolButton {text: "Duplicate"; onClicked: controller.duplicateTab(controller.currentIndex)}
                ToolButton {text: "◀"; enabled: controller.currentIndex > 0; onClicked: controller.moveTab(controller.currentIndex,controller.currentIndex-1)}
                ToolButton {text: "▶"; enabled: controller.currentIndex >= 0 && controller.currentIndex < controller.tabs.length-1; onClicked: controller.moveTab(controller.currentIndex,controller.currentIndex+1)}
                Item {Layout.fillWidth: true}
                ToolButton {text: "Settings"; onClicked: settingsDialog.open()}
            }
        }
    }
    ColumnLayout {
        anchors.fill: parent; spacing: 0
        Rectangle {
            id: searchPanel
            visible: false; Layout.fillWidth: true; Layout.preferredHeight: 42; color: window.panelColor
            RowLayout {
                anchors.fill: parent; anchors.margins: 4
                TextField {
                    id: searchField
                    Layout.fillWidth: true; placeholderText: "Search terminal history…"
                    onAccepted: window.search(false)
                    Keys.onEscapePressed: window.toggleFind()
                }
                Button {text: "Previous"; onClicked: window.search(true)}
                Button {text: "Next"; onClicked: window.search(false)}
                Label {id: searchResult; color: window.mutedColor}
                ToolButton {text: "×"; onClicked: window.toggleFind()}
            }
        }
        Item {
            Layout.fillHeight: true; Layout.fillWidth: true
            PaneTree {
                anchors.fill: parent
                node: controller.currentRoot
                host: window
            }
            Label {
                anchors.centerIn: parent; visible: !controller.currentRoot
                text: "No terminal session. Select an installed shell."
                color: window.mutedColor
            }
        }
    }
    footer: Rectangle {
        implicitHeight: 25; color: window.panelColor
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 7; anchors.rightMargin: 7
            Label {text: controller.status || (controller.active ? controller.active.status : "Ready"); color:window.mutedColor; Layout.fillWidth:true; elide:Text.ElideRight; font.pixelSize:11}
            Label {text: "Ctrl-click links · Shift-drag to select in mouse-enabled apps"; color:window.mutedColor; font.pixelSize:11}
        }
    }
    Dialog {
        id: renameDialog
        anchors.centerIn: Overlay.overlay; width: 350; title: "Rename tab"; modal:true
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {id: renameField; width:renameDialog.availableWidth; onAccepted:renameDialog.accept()}
        onOpened: {renameField.forceActiveFocus();renameField.selectAll()}
        onAccepted: controller.renameTab(controller.currentIndex,renameField.text)
        onClosed: window.focusTerminal()
    }
    Dialog {
        id: settingsDialog
        anchors.centerIn: Overlay.overlay
        width: Math.min(window.width-40,580); height: Math.min(window.height-40,680)
        modal: true; title: "Terminal settings"; standardButtons: Dialog.Close
        onClosed: window.focusTerminal()
        ScrollView {
            anchors.fill: parent; clip: true
            ColumnLayout {
                width: settingsDialog.availableWidth-22; spacing:8
                Label {text:"Appearance"; font.pixelSize:18; font.weight:Font.DemiBold}
                RowLayout {
                    Label {text:"Theme"; Layout.fillWidth:true}
                    ComboBox {model:["System","Light","Dark"]; currentIndex:model.indexOf(controller.options.theme); onActivated:controller.setOption("theme",currentText)}
                }
                RowLayout {
                    Label {text:"Font family"; Layout.fillWidth:true}
                    TextField {text:controller.options.font; Layout.preferredWidth:210; onEditingFinished:controller.setOption("font",text)}
                    Button {text:"Choose"; onClicked:fontDialog.open()}
                    FontDialog {id:fontDialog; onAccepted:controller.setOption("font",selectedFont.family)}
                }
                RowLayout {
                    Label {text:"Font size"; Layout.fillWidth:true}
                    SpinBox {from:8;to:40;editable:true;value:controller.options.fontSize;onValueModified:controller.setOption("fontSize",value)}
                }
                Repeater {
                    model:[{key:"foreground",label:"Foreground"},{key:"background",label:"Background"},{key:"cursorColor",label:"Cursor color"}]
                    delegate:RowLayout {
                        required property var modelData
                        Layout.fillWidth:true
                        Label {text:modelData.label; Layout.fillWidth:true}
                        Rectangle {width:26;height:22;color:controller.options[modelData.key];border.color:window.lineColor}
                        TextField {text:controller.options[modelData.key];Layout.preferredWidth:145;onEditingFinished:controller.setOption(modelData.key,text)}
                    }
                }
                RowLayout {
                    Label {text:"Cursor shape"; Layout.fillWidth:true}
                    ComboBox {model:["Application","Block","Underline","Beam"];currentIndex:model.indexOf(controller.options.cursor);onActivated:controller.setOption("cursor",currentText)}
                }
                CheckBox {text:"Blink cursor";checked:controller.options.blink;onToggled:controller.setOption("blink",checked)}
                RowLayout {
                    Label {text:"Background opacity"; Layout.fillWidth:true}
                    Slider {from:.25;to:1;value:controller.options.opacity;onMoved:controller.setOption("opacity",value)}
                    Label {text:Math.round(controller.options.opacity*100)+"%"}
                }
                Label {text:"Sessions";font.pixelSize:18;font.weight:Font.DemiBold}
                RowLayout {
                    Label {text:"Scrollback lines (per pane, also capped by memory)";Layout.fillWidth:true;wrapMode:Text.Wrap}
                    SpinBox {from:0;to:100000;stepSize:1000;editable:true;value:controller.options.scrollback;onValueModified:controller.setOption("scrollback",value)}
                }
                CheckBox {text:"Restore tabs on startup";checked:controller.options.restoreTabs;onToggled:controller.setOption("restoreTabs",checked)}
                CheckBox {text:"Restore split pane layouts";checked:controller.options.restorePanes;onToggled:controller.setOption("restorePanes",checked)}
                CheckBox {text:"Inherit current working directory";checked:controller.options.inheritCwd;onToggled:controller.setOption("inheritCwd",checked)}
                Button {text:"Refresh installed shells";onClicked:controller.refreshShells()}
                Button {text:"Clear active pane history";enabled:!!controller.active;onClicked:controller.active.clearHistory()}
                Button {text:"Select all in active pane";enabled:!!window.activeView;onClicked:{window.activeView.selectAll();settingsDialog.close()}}
                Label {text:"Keyboard shortcuts";font.pixelSize:18;font.weight:Font.DemiBold}
                Repeater {
                    model:[
                        {key:"newTabKey",label:"New tab"},{key:"closePaneKey",label:"Close pane"},
                        {key:"splitHKey",label:"Split left/right"},{key:"splitVKey",label:"Split top/bottom"},
                        {key:"copyKey",label:"Copy"},{key:"pasteKey",label:"Paste"},{key:"findKey",label:"Search"},
                        {key:"nextTabKey",label:"Next tab"},{key:"prevTabKey",label:"Previous tab"},
                        {key:"duplicateKey",label:"Duplicate tab"},{key:"renameKey",label:"Rename tab"},{key:"settingsKey",label:"Settings"}
                    ]
                    delegate:RowLayout {
                        required property var modelData
                        Layout.fillWidth:true
                        Label {text:modelData.label;Layout.fillWidth:true}
                        TextField {text:controller.options[modelData.key];Layout.preferredWidth:175;onEditingFinished:controller.setOption(modelData.key,text)}
                    }
                }
                Label {text:"Updates";font.pixelSize:18;font.weight:Font.DemiBold}
                Label {text:"Trusted HTTPS release manifest (optional)";color:window.mutedColor}
                TextField {text:controller.options.feed;Layout.fillWidth:true;onEditingFinished:controller.setOption("feed",text)}
                Label {text:updater.status;Layout.fillWidth:true;wrapMode:Text.Wrap;color:window.mutedColor}
                RowLayout {
                    Button {text:"Check updates";onClicked:updater.check()}
                    Button {text:"Install downloaded update";enabled:updater.ready;onClicked:updater.install()}
                }
            }
        }
    }
}
