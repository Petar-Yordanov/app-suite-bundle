import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AppSuite.Monitor 1.0

ApplicationWindow {
    id: win
    width: 1260; height: 800; minimumWidth: 760; minimumHeight: 500
    visible: true; title: "System Monitor"
    property bool dark: controller.options.theme === "Dark" || (controller.options.theme === "System" && Qt.styleHints.colorScheme === Qt.Dark)
    property color baseColor: dark ? "#181b21" : "#f6f7fa"
    property color panelColor: dark ? "#22262e" : "#ffffff"
    property color lineColor: dark ? "#363c47" : "#dce0e8"
    property color muted: dark ? "#a0aabd" : "#59677a"
    property color selection: dark ? "#29466d" : "#d8e8ff"
    property color accent: dark ? "#8db7ff" : "#235aaa"
    property var chosen: controller.selected
    property string activeEntryType: tabs.currentIndex === 2 ? "startup" : "services"
    property string selectedEntryId: ""
    property var selectedEntry: ({})
    property string confirmText: ""
    property var pendingOperation: null
    color: baseColor
    onClosing: function(close) {if(controller.busy)close.accepted=false}
    palette.window: baseColor; palette.base: panelColor; palette.button: panelColor
    palette.text: dark ? "#e6eaf2" : "#202737"; palette.windowText: palette.text
    palette.buttonText: palette.text; palette.highlight: selection; palette.highlightedText: palette.text
    function unavailable(v) { return v === null || v === undefined || (typeof v === "number" && !isFinite(v)) }
    function number(v, decimals) { return unavailable(v) ? "—" : Number(v).toFixed(decimals) }
    function capacity(v) {
        if (unavailable(v)) return "—"
        const units = ["B", "KiB", "MiB", "GiB", "TiB"]; let n = Number(v); let i = 0
        while (n >= 1024 && i < units.length-1) { n /= 1024; ++i }
        return n.toFixed(i ? 1 : 0) + " " + units[i]
    }
    function throughput(v) { return unavailable(v) ? "—" : capacity(v) + "/s" }
    function duration(v) {
        if (unavailable(v)) return "—"
        let s = Math.max(0, Math.floor(v)); let d = Math.floor(s/86400); s %= 86400
        let h = Math.floor(s/3600); s %= 3600; let m = Math.floor(s/60)
        return (d ? d+"d " : "") + h+"h "+m+"m"
    }
    function format(v, unit) {
        if (unavailable(v)) return "Unavailable"
        if (unit === "B") return capacity(v)
        if (unit === "B/s") return throughput(v)
        return number(v, 1) + " " + unit
    }
    function confirmOperation(text, fn) { confirmText = text; pendingOperation = fn; confirmation.open() }
    function requestAction(action, priority, cores) {
        if (!chosen.pid || chosen.identity === "0") return
        let elevated = elevatedBox.checked
        let text = action + " " + chosen.processName + " (PID " + chosen.pid + ")?"
        if (elevated) text += "\nAdministrator authorization will be requested."
        confirmOperation(text, function() { controller.processAction(action, priority, cores, elevated) })
    }
    function refreshPage() {
        if (tabs.currentIndex >= 2) controller.loadEntries(activeEntryType)
        else controller.refresh()
    }
    Shortcut {sequence: controller.options.refreshKey; onActivated: win.refreshPage()}
    Shortcut {sequence: controller.options.searchKey; onActivated: {tabs.currentIndex=0; search.forceActiveFocus(); search.selectAll()}}
    Shortcut {sequence: controller.options.settingsKey; onActivated: settings.open()}
    header: Rectangle {
        implicitHeight: 57; color: panelColor
        RowLayout {
            anchors.fill: parent; anchors.leftMargin: 14; anchors.rightMargin: 14
            Label {text: "System Monitor"; font.pixelSize: 20; font.weight: Font.DemiBold}
            Item {Layout.fillWidth: true}
            Label {text: controller.processCount + " processes"; color: muted}
            ToolButton {text: "Refresh"; onClicked: win.refreshPage()}
            ToolButton {text: "Settings"; onClicked: settings.open()}
        }
        Rectangle {anchors.bottom: parent.bottom; width: parent.width; height: 1; color: lineColor}
    }
    ColumnLayout {
        anchors.fill: parent; anchors.margins: 12; spacing: 8
        TabBar {
            id: tabs; Layout.fillWidth: true
            TabButton {text: "Processes"}
            TabButton {text: "Resources"}
            TabButton {text: "Startup"}
            TabButton {text: "Services"}
            onCurrentIndexChanged: {
                win.selectedEntryId="";win.selectedEntry=({})
                if(currentIndex>=2)controller.loadEntries(win.activeEntryType)
            }
        }
        StackLayout {
            currentIndex: tabs.currentIndex; visible: tabs.currentIndex<2; Layout.fillWidth: true; Layout.fillHeight: true
            Item {
                ColumnLayout {
                    anchors.fill: parent; spacing: 8
                    RowLayout {
                        TextField {id: search; Layout.fillWidth: true; placeholderText: "Search name, PID, user, command or executable"; onTextChanged: controller.processes.query(text)}
                        Label {text: "Sort:"}
                        ComboBox {
                            id: sortRole; textRole: "label"; valueRole: "role"
                            model: [{label:"CPU",role:"cpu"},{label:"Memory",role:"memory"},{label:"Name",role:"processName"},{label:"PID",role:"pid"},{label:"User",role:"user"},{label:"Disk read",role:"diskRead"},{label:"Disk write",role:"diskWrite"},{label:"Threads",role:"threads"},{label:"Uptime",role:"uptime"},{label:"Virtual memory",role:"virtualMemory"},{label:"Network upload",role:"networkUp"},{label:"Network download",role:"networkDown"}]
                            onActivated: controller.processes.order(currentValue,descending.checked)
                        }
                        CheckBox {id: descending; text: "Descending"; checked: true; onToggled: controller.processes.order(sortRole.currentValue,checked)}
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.fillHeight: true; color: panelColor; border.color: lineColor; radius: 5; clip: true
                        Flickable {
                            id: horizontal; anchors.fill: parent; anchors.margins: 1; contentWidth: Math.max(width, 1170); contentHeight: height
                            flickableDirection: Flickable.HorizontalFlick; boundsBehavior: Flickable.StopAtBounds; clip: true
                            ScrollBar.horizontal: ScrollBar {policy: ScrollBar.AsNeeded}
                            ColumnLayout {
                                width: horizontal.contentWidth; height: horizontal.height-12; spacing: 0
                                Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 34; color: baseColor
                                    Row {
                                        anchors.fill: parent
                                        Repeater {
                                            model: [{name:"Name",role:"processName",w:240},{name:"PID",role:"pid",w:70},{name:"User",role:"user",w:155},{name:"CPU %",role:"cpu",w:80},{name:"Memory",role:"memory",w:106},{name:"Disk read",role:"diskRead",w:116},{name:"Disk write",role:"diskWrite",w:116},{name:"Threads",role:"threads",w:74},{name:"Uptime",role:"uptime",w:110}]
                                            delegate: ToolButton {
                                                required property var modelData
                                                width: modelData.w; height: 34; text: modelData.name
                                                onClicked: controller.processes.order(modelData.role,descending.checked)
                                            }
                                        }
                                    }
                                }
                                ListView {
                                    id: processList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true; cacheBuffer: 200
                                    model: controller.processes
                                    ScrollBar.vertical: ScrollBar {policy: ScrollBar.AsNeeded}
                                    delegate: Rectangle {
                                        id: row
                                        required property int index
                                        required property var pid
                                        required property string identity
                                        required property string processName
                                        required property string user
                                        required property var cpu
                                        required property var memory
                                        required property var diskRead
                                        required property var diskWrite
                                        required property var threads
                                        required property var uptime
                                        width: processList.width; height: 30
                                        color: win.chosen.pid === pid && win.chosen.identity===identity ? selection : index%2 ? baseColor : panelColor
                                        Row {
                                            anchors.fill: parent
                                            Repeater {
                                                model: [{v:row.processName,w:240},{v:String(row.pid),w:70},{v:row.user||"—",w:155},{v:win.number(row.cpu,1),w:80},{v:win.capacity(row.memory),w:106},{v:win.throughput(row.diskRead),w:116},{v:win.throughput(row.diskWrite),w:116},{v:win.number(row.threads,0),w:74},{v:win.duration(row.uptime),w:110}]
                                                Label {
                                                    required property var modelData
                                                    width: modelData.w; height: row.height; leftPadding: 8; rightPadding: 4
                                                    text: modelData.v; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                                                }
                                            }
                                        }
                                        MouseArea {anchors.fill: parent; onClicked:controller.select(row.pid,row.identity)}
                                    }
                                }
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true; Layout.preferredHeight: 205; radius: 5; color: panelColor; border.color: lineColor
                        ColumnLayout {
                            anchors.fill: parent; anchors.margins: 10; spacing: 4
                            Label {text: chosen.pid === undefined ? "Select a process to inspect or manage" : chosen.processName + "  •  PID " + chosen.pid + "  •  Parent " + chosen.parentPid; font.weight: Font.DemiBold; Layout.fillWidth: true; elide: Text.ElideRight}
                            Label {text: "Executable: " + (chosen.path || "Unavailable"); Layout.fillWidth: true; elide: Text.ElideMiddle}
                            TextField {text: chosen.command || "Command line unavailable"; readOnly: true; Layout.fillWidth: true; selectByMouse: true; font.pixelSize: 12}
                            Label {
                                text: "Virtual/private memory: " + win.capacity(chosen.virtualMemory) + "  •  Network: ↑ " + win.throughput(chosen.networkUp) + "  ↓ " + win.throughput(chosen.networkDown)
                                Layout.fillWidth: true; elide: Text.ElideRight; color: muted
                            }
                            Flow {
                                Layout.fillWidth: true; spacing: 4
                                enabled: chosen.pid !== undefined && chosen.identity !== "0" && !controller.busy
                                Button {text: "Terminate"; onClicked: win.requestAction("terminate",0,"")}
                                Button {text: "Force terminate"; onClicked: win.requestAction("force",0,"")}
                                Button {text: "End tree"; onClicked: win.requestAction("tree",0,"")}
                                Button {text: "Suspend"; onClicked: win.requestAction("suspend",0,"")}
                                Button {text: "Resume"; onClicked: win.requestAction("resume",0,"")}
                                Button {text: "Priority…"; onClicked: priorityDialog.open()}
                                Button {text: "Affinity…"; enabled: Qt.platform.os !== "osx"; onClicked: affinityDialog.open()}
                                CheckBox {id: elevatedBox; text: "Elevated"}
                            }
                        }
                    }
                }
            }
            Rectangle {
                color: panelColor; radius: 5; border.color: lineColor
                ListView {
                    id: metrics; anchors.fill: parent; anchors.margins: 10; clip: true; reuseItems: true
                    model: controller.metrics; ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property int index
                        required property string label
                        required property var value
                        required property string unit
                        required property string detail
                        width: metrics.width; height: 42; color: index%2?baseColor:panelColor
                        RowLayout {
                            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 12
                            Label {text: label; font.weight: Font.Medium; Layout.preferredWidth: 280; elide: Text.ElideRight}
                            Label {text: win.format(value,unit); Layout.preferredWidth: 150; color: win.unavailable(value)?muted:accent; font.weight: Font.DemiBold}
                            Label {text: detail; color: muted; Layout.fillWidth: true; elide: Text.ElideRight}
                        }
                    }
                }
            }
            // The same virtualized list is used by both management tabs.
            Item {id: startupPage}
            Item {id: servicesPage}
        }
        Rectangle {
            Layout.fillWidth: true; Layout.fillHeight: tabs.currentIndex>=2; visible: tabs.currentIndex>=2
            color: panelColor; border.color: lineColor; radius: 5
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 10
                RowLayout {
                    Label {text: tabs.currentIndex===2?"Startup applications":"System services"; font.weight: Font.DemiBold; Layout.fillWidth: true}
                    Button {text: "Refresh"; onClicked: controller.loadEntries(activeEntryType)}
                }
                ListView {
                    id: managementList; Layout.fillWidth: true; Layout.fillHeight: true; clip: true; reuseItems: true
                    model: controller.entries; ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property int index; required property var modelData
                        width: managementList.width; height: 54
                        color: win.selectedEntryId===modelData.id && modelData.id!==""?selection:index%2?baseColor:panelColor
                        RowLayout {
                            anchors.fill: parent; anchors.margins: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                Label {text: modelData.name; Layout.fillWidth: true; elide: Text.ElideRight; font.weight: Font.Medium}
                                Label {text: modelData.location; color: muted; Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 12}
                            }
                            Label {text: modelData.state; color: modelData.editable?accent:muted}
                        }
                        MouseArea {anchors.fill: parent; onClicked:{win.selectedEntryId=modelData.id;win.selectedEntry=modelData}}
                    }
                }
                Label {text: selectedEntry.name || "Select an entry"; Layout.fillWidth: true; elide: Text.ElideRight}
                RowLayout {
                    enabled: !!selectedEntry.editable && !controller.busy
                    Button {
                        text: tabs.currentIndex===2?"Enable":"Start"
                        onClicked: {let type=activeEntryType;let id=selectedEntryId;let op=tabs.currentIndex===2?"enable":"start";win.confirmOperation(op+" "+selectedEntry.name+"?",function(){controller.entryAction(type,id,op)})}
                    }
                    Button {
                        text: tabs.currentIndex===2?"Disable":"Stop"
                        onClicked: {let type=activeEntryType;let id=selectedEntryId;let op=tabs.currentIndex===2?"disable":"stop";win.confirmOperation(op+" "+selectedEntry.name+"?",function(){controller.entryAction(type,id,op)})}
                    }
                }
            }
        }
        Label {text: controller.warning; color: muted; Layout.fillWidth: true; wrapMode: Text.WordWrap; font.pixelSize: 11}
    }
    footer: Rectangle {
        implicitHeight: 36; color: panelColor
        Label {anchors.fill: parent; anchors.margins: 9; text: controller.status; elide: Text.ElideRight}
    }
    Dialog {
        id: confirmation; modal: true; anchors.centerIn: parent; title: "Confirm operation"; standardButtons: Dialog.Yes|Dialog.No
        contentItem: Label {text: win.confirmText; wrapMode: Text.WordWrap; width: 380}
        onAccepted: {if(win.pendingOperation)win.pendingOperation();win.pendingOperation=null}
        onRejected: win.pendingOperation=null
    }
    Dialog {
        id: priorityDialog; anchors.centerIn: parent; modal: true; title:"Change priority"; standardButtons: Dialog.Ok|Dialog.Cancel
        ColumnLayout {
            Label {text:"Real-time scheduling is not offered."}
            ComboBox {id: priority; model:["Idle","Below normal","Normal","Above normal","High"];currentIndex:2}
        }
        onAccepted: win.requestAction("priority",priority.currentIndex-2,"")
    }
    Dialog {
        id: affinityDialog; anchors.centerIn:parent; modal:true; title:"CPU affinity"; standardButtons:Dialog.Ok|Dialog.Cancel
        ColumnLayout {
            Label {text:"CPU indices: 0–"+(controller.coreCount-1)}
            TextField {id: cores; placeholderText:"0,1,2,3"; Layout.fillWidth:true}
        }
        onAccepted: win.requestAction("affinity",0,cores.text)
    }
    Dialog {
        id: settings; title:"Settings"; anchors.centerIn:parent; modal:true; standardButtons:Dialog.Close
        width: Math.min(win.width-40,540)
        ColumnLayout {
            width: parent.width
            Label {text:"Theme"}
            ComboBox {model:["Light","Dark","System"];currentIndex:model.indexOf(controller.options.theme);onActivated:controller.setOption("theme",currentText);Layout.fillWidth:true}
            Label {text:"Refresh interval (milliseconds)"}
            SpinBox {from:250;to:10000;stepSize:250;value:controller.options.interval;editable:true;onValueModified:controller.setOption("interval",value)}
            Button {text:"Raise monitor scheduling priority";enabled:!controller.busy;onClicked:{settings.close();win.confirmOperation("Raise this monitor's scheduling priority? Administrator authorization may be requested.",function(){controller.raisePriority()})}}
            Repeater {
                model:[{label:"Refresh shortcut",key:"refreshKey"},{label:"Search shortcut",key:"searchKey"},{label:"Settings shortcut",key:"settingsKey"}]
                RowLayout {
                    required property var modelData
                    Layout.fillWidth:true
                    Label {text:modelData.label;Layout.fillWidth:true}
                    TextField {text:controller.options[modelData.key];onEditingFinished:controller.setOption(modelData.key,text)}
                }
            }
            Label {text:"Trusted HTTPS update manifest"}
            TextField {text:controller.options.feed;Layout.fillWidth:true;placeholderText:"https://…/release.json";onEditingFinished:controller.setOption("feed",text)}
            Label {text:updater.status;wrapMode:Text.WordWrap;Layout.fillWidth:true}
            RowLayout {Button {text:"Check for updates";onClicked:updater.check()} Button {text:"Open installer";enabled:updater.ready;onClicked:updater.install()}}
        }
    }
}
