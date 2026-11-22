import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

ApplicationWindow {
    id: window
    width: 1120; height: 740
    minimumWidth: 820; minimumHeight: 540
    visible: true
    title: "Docker GUI"
    property bool dark: controller.theme === "Dark" || (controller.theme === "System" && Qt.styleHints.colorScheme === Qt.Dark)
    color: dark ? "#16181c" : "#f4f5f7"
    palette.window: color
    palette.base: dark ? "#20232a" : "#ffffff"
    palette.alternateBase: dark ? "#262a32" : "#edf0f4"
    palette.button: palette.alternateBase
    palette.text: dark ? "#e4e7ec" : "#20242b"
    palette.buttonText: palette.text
    palette.windowText: palette.text
    palette.highlight: dark ? "#305c9c" : "#c6dcff"
    palette.highlightedText: dark ? "#ffffff" : "#142640"
    property color subtle: dark ? "#a4adbc" : "#5b6573"
    property color border: dark ? "#333842" : "#d9dde4"
    property var selected: ({})
    property string selectedId: selected.id || ""
    property var pages: ["Containers","Images","Volumes","Networks","Compose","Storage","Statistics"]
    property var currentFields: []
    property string pendingVerb: ""
    property string pendingTitle: ""
    property var pendingRow: ({})
    onVisibilityChanged: controller.setVisible(visible && visibility !== Window.Minimized)
    function choosePage(page) {
        selected = ({})
        controller.selectPage(page)
    }
    function field(key, label, value, type) {
        return { key:key, label:label, value:value || "", type:type || "text" }
    }
    function form(verb, title, fields, row) {
        pendingVerb=verb; pendingTitle=title; pendingRow=row || ({})
        currentFields=fields; formDialog.open()
    }
    function confirm(verb, text, fields) {
        pendingVerb=verb; pendingRow=selected
        confirmDialog.message=text; confirmDialog.fields=fields || ({})
        confirmDialog.open()
    }
    function create() {
        switch(controller.page) {
        case "Containers":
            form("createContainer","Create container",[
                field("image","Image (required)","alpine:latest"),
                field("name","Container name",""),
                field("env","Environment variables (KEY=value, one per line)","","multi"),
                field("mounts","Mounts (Docker --mount syntax, one per line)","","multi"),
                field("ports","Port mappings (host:container, one per line)","","multi"),
                field("restart","Restart policy (no / always / unless-stopped / on-failure)","no"),
                field("command","Command and arguments (one argument per line)","","multi"),
                field("start","Start immediately","true","check")]); break
        case "Images":
            form("pull","Pull image",[field("image","Image reference","")]); break
        case "Volumes":
            form("createVolume","Create volume",[field("name","Volume name",""),field("driver","Driver","local")]); break
        case "Networks":
            form("createNetwork","Create network",[field("name","Network name",""),field("driver","Driver","bridge")]); break
        case "Compose":
            compose("composeUp","Deploy Compose project",{}); break
        }
    }
    function compose(verb,title,row) {
        form(verb,title,[field("project","Project name",row.id || ""),
            field("files","Compose files (absolute local paths, one per line)",(row.detail || "").split(",").join("\n"),"multi")],row)
    }
    function perform(verb) {controller.action(verb,selected,{})}
    function actionForm(action) {
        if(action==="tag")form("tag","Tag image",[field("tag","New repository:tag","")],selected)
        else if(action==="push")form("push","Push image",[field("tag","Repository:tag",selected.title || "")],selected)
        else if(action==="build")form("build","Build image",[
            field("directory","Build-context directory","","directory"),field("dockerfile","Dockerfile (optional)","","file"),field("tag","Image tag","")])
        else if(action==="login")form("login","Registry authentication",[
            field("registry","Registry host (blank = Docker Hub)",""),field("user","Username",""),field("password","Password or token","","password")])
        else if(action==="logout")form("logout","Registry sign out",[field("registry","Registry host (blank = Docker Hub)","")])
        else if(action==="terminal")form("terminal","Open interactive terminal",[field("shell","Shell executable","/bin/sh")],selected)
        else if(action==="configure")form("configure","Update existing container",[field("restart","Restart policy","unless-stopped")],selected)
        else if(action==="browse"){
            browserDialog.container=selected.id
            browserDialog.open()
            controller.browse(selected.id,"/")
        }
        else if(action==="copyOut" || action==="copyIn")form(action,action==="copyOut"?"Copy from container":"Copy to container",[
            field("path","Container file/directory","/"),field("local","Local path","")],selected)
        else if(action==="context")form("createContext","Add Docker context",[field("name","Context name",""),field("host","SSH or Unix socket endpoint","ssh://user@host")])
        else if(action==="wsl")form("wsl","Start installed WSL runtime",[field("distribution","WSL distribution name","Ubuntu")])
    }
    Shortcut {sequence: controller.refreshKey; onActivated: controller.refresh()}
    Shortcut {sequence: controller.settingsKey; onActivated: settingsDialog.open()}
    header: Rectangle {
        implicitHeight: 58; color: window.palette.base
        RowLayout {
            anchors.fill: parent; anchors.margins: 10; spacing: 10
            Label {text:"Docker GUI"; font.pixelSize: 20; font.weight: Font.DemiBold}
            Item {Layout.fillWidth:true}
            Rectangle {width: 9; height: 9; radius: 5; color:controller.online?"#25a36f":"#d69a45"}
            ComboBox {
                id: contexts
                Layout.preferredWidth: 205
                textRole: "name"; valueRole: "name"
                model: controller.contexts
                currentIndex: {
                    for(let i=0;i<controller.contexts.length;++i)if(controller.contexts[i].name===controller.context)return i
                    return -1
                }
                enabled: !controller.busy
                onActivated: {
                    window.selected=({})
                    controller.switchContext(currentValue)
                }
            }
            Button {text:"Discover"; enabled:!controller.busy; onClicked:controller.discover()}
            Button {text:"Start engine"; enabled:!controller.busy && !controller.online; onClicked:controller.startEngine("")}
            Button {text:"Settings"; onClicked:settingsDialog.open()}
        }
    }
    RowLayout {
        anchors.fill:parent; spacing:0
        Rectangle {
            Layout.fillHeight:true; Layout.preferredWidth: 176
            color:window.palette.base
            ColumnLayout {
                anchors.fill:parent; anchors.margins:12; spacing:4
                Label {text:"RESOURCES"; color:window.subtle; font.pixelSize:11; font.weight:Font.DemiBold; Layout.bottomMargin:8}
                Repeater {
                    model:window.pages
                    delegate: Button {
                        required property string modelData
                        Layout.fillWidth:true
                        text:modelData
                        checkable:true; checked:controller.page===modelData
                        onClicked:window.choosePage(modelData)
                    }
                }
                Item {Layout.fillHeight:true}
                Button {text:"Registry login"; Layout.fillWidth:true; enabled:!controller.busy; onClicked:window.actionForm("login")}
                Button {text:"Registry logout"; Layout.fillWidth:true; enabled:!controller.busy; onClicked:window.actionForm("logout")}
                Button {text:"Add context"; Layout.fillWidth:true; enabled:!controller.busy; onClicked:window.actionForm("context")}
                Button {text:"Start WSL engine"; visible:Qt.platform.os==="windows"; Layout.fillWidth:true; enabled:!controller.busy&&!controller.online; onClicked:window.actionForm("wsl")}
                Button {text:"Prune resources"; Layout.fillWidth:true; enabled:!controller.busy; onClicked:pruneDialog.open()}
            }
        }
        ColumnLayout {
            Layout.fillWidth:true; Layout.fillHeight:true; Layout.margins:16; spacing:8
            RowLayout {
                Layout.fillWidth:true
                Label {text:controller.page; font.pixelSize:25; font.weight:Font.DemiBold}
                Item {Layout.fillWidth:true}
                Button {text:"Refresh"; onClicked:controller.refresh()}
                Button {
                    visible:controller.page!=="Statistics" && controller.page!=="Storage"
                    text:controller.page==="Images"?"Pull image":controller.page==="Compose"?"Deploy":"Create"
                    enabled:!controller.busy && controller.online; onClicked:window.create()
                }
                Button {text:"Build image"; visible:controller.page==="Images"; enabled:!controller.busy&&controller.online; onClicked:window.actionForm("build")}
            }
            Label {
                Layout.fillWidth:true
                text:controller.page==="Statistics"?"Live snapshots · disk I/O is cumulative; writable layer size is available from Containers.":
                    controller.page==="Storage"?"Engine-reported disk consumption · shared image layers are not summed twice.":
                    controller.page==="Compose"?"Projects discovered in the current context. Compose file paths must be accessible locally.":"Select a resource to manage it."
                color:window.subtle; wrapMode:Text.Wrap
            }
            Rectangle {
                Layout.fillWidth:true; Layout.fillHeight:true; Layout.minimumHeight:140
                color:window.palette.base; border.color:window.border; radius:7
                ListView {
                    id: resources
                    anchors.fill:parent; anchors.margins:1; clip:true
                    model:controller.resources; reuseItems:true; cacheBuffer:280
                    ScrollBar.vertical:ScrollBar {}
                    delegate: ItemDelegate {
                        required property var entry
                        width:ListView.view.width
                        height:60
                        highlighted:window.selected.key===entry.key
                        onClicked:window.selected=entry
                        onDoubleClicked: {
                            window.selected=entry
                            if(controller.page!=="Statistics"&&controller.page!=="Storage"&&controller.page!=="Compose")window.perform("inspect")
                        }
                        contentItem:RowLayout {
                            spacing:12
                            ColumnLayout {
                                Layout.fillWidth:true; Layout.preferredWidth: 320; spacing:2
                                Label {text:entry.title; font.weight:Font.DemiBold; Layout.fillWidth:true; elide:Text.ElideRight}
                                Label {text:entry.sub; color:window.subtle; font.pixelSize:12; Layout.fillWidth:true; elide:Text.ElideRight}
                            }
                            ColumnLayout {
                                Layout.fillWidth:true; Layout.preferredWidth:260; spacing:2
                                Label {text:entry.status; Layout.fillWidth:true; elide:Text.ElideRight}
                                Label {text:entry.detail; color:window.subtle; font.pixelSize:12; Layout.fillWidth:true; elide:Text.ElideRight}
                            }
                        }
                    }
                    Label {
                        anchors.centerIn:parent
                        visible:resources.count===0
                        text:controller.online?"No resources":"Waiting for Docker Engine"
                        color:window.subtle
                    }
                }
            }
            Label {text:selectedId?("Selected: "+selected.title):"No selection"; color:window.subtle; Layout.fillWidth:true; elide:Text.ElideRight}
            Flow {
                id: actions
                Layout.fillWidth:true; spacing:5
                property bool available:window.selectedId!=="" && !controller.busy
                Repeater {
                    model: controller.page==="Containers"?["Start","Stop","Restart","Pause","Resume","Inspect","Configure","Logs","Terminal","Browse files","Copy out","Copy in","Storage size","Delete"]:
                        controller.page==="Images"?["Inspect","Tag","Push","Delete"]:
                        controller.page==="Volumes"||controller.page==="Networks"?["Inspect","Delete"]:
                        controller.page==="Compose"?["Up","Down","Start","Stop","Logs"]:[]
                    delegate: Button {
                        required property string modelData
                        text:modelData; enabled:actions.available
                        onClicked: {
                            if(controller.page==="Compose") {
                                let verbs={"Up":"composeUp","Down":"composeDown","Start":"composeStart","Stop":"composeStop","Logs":"composeLogs"}
                                window.compose(verbs[text],text+" Compose project",window.selected)
                            } else if(text==="Delete")window.confirm("delete","Delete "+window.selected.title+"? Docker will reject resources in use.")
                            else if(text==="Resume")window.perform("unpause")
                            else if(text==="Storage size")window.perform("writableSize")
                            else if(text==="Browse files")window.actionForm("browse")
                            else if(text==="Copy out")window.actionForm("copyOut")
                            else if(text==="Copy in")window.actionForm("copyIn")
                            else if(["Tag","Push","Configure","Terminal"].indexOf(text)>=0)window.actionForm(text.toLowerCase())
                            else window.perform(text.toLowerCase())
                        }
                    }
                }
            }
            RowLayout {
                Label {text:"Output"; font.weight:Font.DemiBold}
                Item {Layout.fillWidth:true}
                Button {text:"Stop logs"; onClicked:controller.stopOutput()}
                Button {text:"Cancel operation"; enabled:controller.busy; onClicked:controller.action("cancel",{},{})}
                Button {text:outputPane.visible?"Hide":"Show"; onClicked:outputPane.visible=!outputPane.visible}
            }
            ScrollView {
                id: outputPane
                Layout.fillWidth:true; Layout.preferredHeight:190; clip:true
                TextArea {
                    id: output
                    text:controller.output; readOnly:true; selectByMouse:true
                    textFormat:TextEdit.PlainText
                    font.family:Qt.platform.os==="windows"?"Consolas":"monospace"
                    font.pixelSize:12; wrapMode:TextEdit.WrapAnywhere
                    onTextChanged:cursorPosition=length
                    placeholderText:"Inspect resources, browse files or view command output here."
                }
            }
        }
    }
    footer: Rectangle {
        implicitHeight:32; color:window.palette.base
        Label {
            anchors.fill:parent; anchors.leftMargin:12; anchors.rightMargin:12
            verticalAlignment:Text.AlignVCenter; elide:Text.ElideRight
            color:window.subtle; text:controller.status
        }
    }
    Dialog {
        id:browserDialog
        property string container:""
        anchors.centerIn:Overlay.overlay; modal:true; title:"Container filesystem"
        width:Math.min(window.width-80,680); height:Math.min(window.height-80,540)
        standardButtons:Dialog.Close
        onClosed:controller.closeBrowser()
        ColumnLayout {
            anchors.fill:parent; spacing:8
            RowLayout {
                Layout.fillWidth:true
                Button {
                    text:"Up"
                    onClicked:{
                        let p=controller.filePath.replace(/\/+$/,"")
                        controller.browse(browserDialog.container,p.substring(0,p.lastIndexOf("/"))||"/")
                    }
                }
                TextField {id:browserPath; Layout.fillWidth:true; text:controller.filePath; onAccepted:controller.browse(browserDialog.container,text)}
                Button {text:"Go"; onClicked:controller.browse(browserDialog.container,browserPath.text)}
            }
            Label {text:controller.fileStatus; Layout.fillWidth:true; wrapMode:Text.Wrap}
            ListView {
                id:fileList
                Layout.fillWidth:true; Layout.fillHeight:true; clip:true; reuseItems:true
                model:controller.files
                ScrollBar.vertical:ScrollBar {}
                delegate:ItemDelegate {
                    required property var modelData
                    width:ListView.view.width; height:40
                    contentItem:RowLayout {
                        Label {text:modelData.directory?"DIR":"FILE"; color:window.subtle; Layout.preferredWidth:43}
                        Label {text:modelData.name; Layout.fillWidth:true; elide:Text.ElideRight}
                        Button {
                            text:"Copy out"
                            onClicked:{
                                let row={id:browserDialog.container}
                                let path=modelData.path
                                browserDialog.close()
                                window.form("copyOut","Copy from container",[window.field("path","Container path",path),window.field("local","Local destination","","directory")],row)
                            }
                        }
                    }
                    onClicked:if(modelData.directory)controller.browse(browserDialog.container,modelData.path)
                }
            }
        }
    }
    Dialog {
        id: formDialog
        anchors.centerIn:Overlay.overlay
        width:Math.min(window.width-80,600)
        height:Math.min(window.height-70,Math.max(240,formColumn.implicitHeight+125))
        modal:true; title:window.pendingTitle
        standardButtons:Dialog.Ok|Dialog.Cancel
        onAccepted: {
            let data={}
            for(let i=0;i<fieldsRepeater.count;++i){
                let item=fieldsRepeater.itemAt(i)
                data[item.modelData.key]=item.value()
                if(item.modelData.type==="password")item.inputText=""
            }
            if(window.pendingVerb==="terminal")controller.terminal(window.pendingRow.id,data.shell)
            else if(window.pendingVerb==="wsl")controller.startEngine(data.distribution)
            else controller.action(window.pendingVerb,window.pendingRow,data)
        }
        onClosed:window.currentFields=[]
        ScrollView {
            anchors.fill:parent; clip:true
            ColumnLayout {
                id:formColumn
                width:formDialog.availableWidth-20; spacing:9
                Repeater {
                    id:fieldsRepeater
                    model:window.currentFields
                    delegate:ColumnLayout {
                        id: fieldRow
                        required property var modelData
                        Layout.fillWidth:true
                        property alias inputText: single.text
                        function value(){return modelData.type==="check"?check.checked:modelData.type==="multi"?multi.text:single.text}
                        Label {text:fieldRow.modelData.label; Layout.fillWidth:true; wrapMode:Text.Wrap}
                        RowLayout {
                            Layout.fillWidth:true
                            visible:!["multi","check"].includes(fieldRow.modelData.type)
                            TextField {
                                id:single; Layout.fillWidth:true; text:fieldRow.modelData.value
                                echoMode:fieldRow.modelData.type==="password"?TextInput.Password:TextInput.Normal
                            }
                            Button {
                                visible:fieldRow.modelData.type==="directory"||fieldRow.modelData.type==="file"
                                text:"Browse…"
                                onClicked: {
                                    if(fieldRow.modelData.type==="directory")folderPicker.open();else filePicker.open()
                                }
                            }
                            FolderDialog {id:folderPicker; onAccepted:single.text=decodeURIComponent(selectedFolder.toString().replace(Qt.platform.os==="windows"?/^file:\/\/\//:/^file:\/\//,""))}
                            FileDialog {id:filePicker; onAccepted:single.text=decodeURIComponent(selectedFile.toString().replace(Qt.platform.os==="windows"?/^file:\/\/\//:/^file:\/\//,""))}
                        }
                        TextArea {
                            id:multi; visible:fieldRow.modelData.type==="multi"
                            text:fieldRow.modelData.value; Layout.fillWidth:true; Layout.preferredHeight:72
                            wrapMode:TextEdit.WrapAnywhere
                        }
                        CheckBox {id:check; visible:fieldRow.modelData.type==="check"; text:"Enabled"; checked:fieldRow.modelData.value==="true"}
                    }
                }
            }
        }
    }
    Dialog {
        id:confirmDialog
        property string message:""
        property var fields:({})
        anchors.centerIn:Overlay.overlay; modal:true; title:"Confirm operation"
        width:440; standardButtons:Dialog.Ok|Dialog.Cancel
        Label {text:confirmDialog.message; wrapMode:Text.Wrap; width:confirmDialog.availableWidth}
        onAccepted:controller.action(window.pendingVerb,window.pendingRow,fields)
    }
    Dialog {
        id:pruneDialog
        anchors.centerIn:Overlay.overlay; modal:true; title:"Prune unused resources"
        width:440; standardButtons:Dialog.Ok|Dialog.Cancel
        ColumnLayout {
            width:pruneDialog.availableWidth
            Label {Layout.fillWidth:true; wrapMode:Text.Wrap; text:"This permanently removes unused resources from the selected engine. System prune removes stopped containers, unused networks, dangling images and build cache. Review your resources first."}
            ComboBox {id:pruneResource; model:["container","image","volume","network","system"]; Layout.fillWidth:true}
            CheckBox {
                id:pruneAll
                visible:pruneResource.currentText==="image" || pruneResource.currentText==="volume"
                text:pruneResource.currentText==="image"?"Include all unused images":"Include unused named volumes"
                checked:false
            }
        }
        onAccepted:window.confirm("prune","Prune "+(pruneAll.visible&&pruneAll.checked?"ALL ":"")+"unused "+pruneResource.currentText+" resources on "+controller.context+"?",{resource:pruneResource.currentText,all:pruneAll.visible&&pruneAll.checked})
    }
    Dialog {
        id:settingsDialog
        anchors.centerIn:Overlay.overlay; modal:true; title:"Settings"; width:510
        standardButtons:Dialog.Close
        ColumnLayout {
            width:settingsDialog.availableWidth; spacing:10
            Label {text:"Appearance"}
            ComboBox {model:["System","Light","Dark"]; currentIndex:model.indexOf(controller.theme); onActivated:controller.theme=currentText; Layout.fillWidth:true}
            RowLayout {
                Label {text:"Refresh interval (seconds)"; Layout.fillWidth:true}
                SpinBox {from:2; to:60; value:controller.interval; editable:true; onValueModified:controller.interval=value}
            }
            CheckBox {text:"Try to start a locally installed runtime automatically"; checked:controller.autoStart; onToggled:controller.autoStart=checked}
            Label {text:"Refresh shortcut"}
            TextField {text:controller.refreshKey; Layout.fillWidth:true; onEditingFinished:controller.refreshKey=text}
            Label {text:"Settings shortcut"}
            TextField {text:controller.settingsKey; Layout.fillWidth:true; onEditingFinished:controller.settingsKey=text}
            Label {text:"HTTPS release manifest (optional)"}
            TextField {text:controller.feed; Layout.fillWidth:true; onEditingFinished:controller.feed=text}
            Label {text:updater.status; Layout.fillWidth:true; wrapMode:Text.Wrap; color:window.subtle}
            RowLayout {
                Button {text:"Check updates"; onClicked:updater.check()}
                Button {text:"Install downloaded update"; enabled:updater.ready; onClicked:updater.install()}
            }
        }
    }
}
