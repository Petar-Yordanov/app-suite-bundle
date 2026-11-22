import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: win
    width: Qt.platform.os==="ios"?Screen.width:1160
    height: Qt.platform.os==="ios"?Screen.height:760
    visible: true
    title: browser.current && browser.current.title ? browser.current.title + " - WebBrowser" : "WebBrowser"
    property string panel: ""
    property bool pageFullScreen: false
    property bool dark: browser.options.theme === "Dark" || (browser.options.theme === "System" && Qt.styleHints.colorScheme === Qt.Dark)
    palette.window: dark ? "#17191c" : "#f5f6f8"
    palette.windowText: dark ? "#e7e8eb" : "#20242b"
    palette.base: dark ? "#202329" : "#ffffff"
    palette.alternateBase: dark ? "#2b2e35" : "#eceef2"
    palette.text: dark ? "#e7e8eb" : "#20242b"
    palette.button: dark ? "#2c3037" : "#e5e8ee"
    palette.buttonText: dark ? "#e7e8eb" : "#20242b"
    palette.highlight: "#3678cb"
    palette.highlightedText: "#ffffff"
    color: palette.window
    function pageFor(id) {
        for(let i=0;i<pages.count;i++) {
            let l=pages.itemAt(i)
            if(l && l.browserTab.id===id) return l.item
        }
        return null
    }
    function activePage() {
        if (!browser.current) return null
        for(let i=0;i<pages.count;i++) {
            let l=pages.itemAt(i)
            if(l && l.browserTab===browser.current) return l.item
        }
        return null
    }
    property int pageRevision: 0
    readonly property var currentPage: { let r=pageRevision; return activePage() }
    function setFullScreen(on) {
        pageFullScreen=on
        if(Qt.platform.os!=="ios") { if(on) showFullScreen(); else showNormal() }
    }
    function toggleFullScreen() {
        if(pageFullScreen && currentPage) currentPage.exitPageFullScreen()
        setFullScreen(!pageFullScreen)
    }
    function showPanel(name) { panel=panel===name?"":name; refresh() }
    function refresh() {
        if(panel==="History") records.model=browser.history(filter.text)
        else if(panel==="Bookmarks") records.model=browser.bookmarks(filter.text)
    }
    function focusAddress() { panel=""; address.forceActiveFocus(); address.selectAll() }
    function go() { panel=""; browser.go(address.text); address.focus=false }
    function editItem(item, folder) {
        editor.record=item
        editor.folder=folder
        editor.nameText=item ? item.title : folder ? "" : browser.current.title
        editor.parents=browser.folders()
        editor.parentId=item ? item.parent : 0
        editor.open()
    }
    Connections { target: browser
        function onCurrentChanged() { address.text=browser.current ? browser.current.url.toString() : ""; win.pageRevision++; if(win.pageFullScreen)win.setFullScreen(false) }
        function onHistoryChanged() { if(win.panel==="History") win.refresh() }
        function onBookmarksChanged() { if(win.panel==="Bookmarks") win.refresh() }
        function onMessageChanged() { toast.text=browser.message; toastTimer.restart() }
    }
    Connections {
        target: browser.current
        function onChanged() { if(!address.activeFocus && browser.current) address.text=browser.current.url.toString() }
    }
    Connections {
        target: Qt.platform.os==="ios" ? null : profiles.normal
        function onDownloadRequested(request) { downloads.acceptDesktop(request,false); win.panel="Downloads" }
        ignoreUnknownSignals: true
    }
    Connections {
        target: Qt.platform.os==="ios" ? null : profiles.privateProfile
        function onDownloadRequested(request) { downloads.acceptDesktop(request,true); win.panel="Downloads" }
        ignoreUnknownSignals: true
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 38
            visible: !win.pageFullScreen
            color: win.palette.window
            RowLayout {
                anchors.fill: parent; spacing: 2
                Flickable {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    contentWidth: tabRow.width; clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    Row {
                        id: tabRow; height: parent.height; spacing: 2
                        Repeater {
                            model: browser
                            delegate: Rectangle {
                                required property var browserTab
                                width: 170; height: 36; radius: 5
                                color: browser.current===browserTab?win.palette.base:win.palette.button
                                border.color: browser.current===browserTab?win.palette.highlight:"transparent"
                                RowLayout {
                                    anchors.fill: parent; anchors.margins: 3; spacing: 1
                                    Button { Layout.fillWidth: true; flat: true; text: (browserTab.privateMode?"[P] ":"")+(browserTab.title || browserTab.url.toString().replace("about:blank","New tab")); onClicked: browser.select(browserTab.id); contentItem: Label { text: parent.text; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter } }
                                    ToolButton { text: "×"; onClicked: browser.closeTab(browserTab.id) }
                                }
                            }
                        }
                    }
                }
                ToolButton { text: "+"; onClicked: { browser.newTab("about:blank",false); win.focusAddress() } }
                ToolButton { text: "P+"; onClicked: { browser.newTab("about:blank",true); win.focusAddress() } }
                ToolButton { text: "‹"; enabled: !!browser.current; onClicked: browser.moveTab(browser.current.id,-1) }
                ToolButton { text: "›"; enabled: !!browser.current; onClicked: browser.moveTab(browser.current.id,1) }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: !win.pageFullScreen
            spacing: 2
            ToolButton { text: "←"; enabled: !!win.currentPage && win.currentPage.canGoBack; onClicked: win.currentPage.goBack() }
            ToolButton { text: "→"; enabled: !!win.currentPage && win.currentPage.canGoForward; onClicked: win.currentPage.goForward() }
            ToolButton { text: win.currentPage && win.currentPage.loading ? "×" : "↻"; enabled: !!win.currentPage; onClicked: win.currentPage.loading?win.currentPage.stop():win.currentPage.reload() }
            TextField {
                id: address; Layout.fillWidth: true
                placeholderText: "Search or enter a URL"; selectByMouse: true
                text: browser.current ? browser.current.url.toString() : ""
                onAccepted: win.go()
            }
            ToolButton { text: "☆"; onClicked: win.editItem(null,false) }
            ToolButton { text: "☰"; onClicked: mainMenu.popup()
                Menu {
                    id: mainMenu
                    MenuItem { text: "Bookmarks"; onTriggered: win.showPanel("Bookmarks") }
                    MenuItem { text: "History"; onTriggered: win.showPanel("History") }
                    MenuItem { text: "Downloads"; onTriggered: win.showPanel("Downloads") }
                    MenuItem { text: "Settings"; onTriggered: win.showPanel("Settings") }
                    MenuItem { text: "Reopen closed tab"; onTriggered: browser.reopen() }
                    MenuItem { text: "Fullscreen"; onTriggered: win.toggleFullScreen() }
                }
            }
        }
        ProgressBar {
            Layout.fillWidth: true; implicitHeight: 3
            visible: !!win.currentPage && win.currentPage.loading
            from: 0; to: 100; value: win.currentPage?win.currentPage.loadProgress:0
        }
        Item {
            id: viewport
            Layout.fillWidth: true; Layout.fillHeight: true
            Repeater {
                id: pages; model: browser
                delegate: Loader {
                    id: pageLoader
                    required property var browserTab
                    anchors.fill: parent
                    active: browserTab.loaded
                    visible: browser.current===browserTab
                    Component.onCompleted: {
                        if (Qt.platform.os==="ios") setSource("IOSPage.qml", {tab:browserTab,browserRoot:win})
                        else setSource("DesktopPage.qml", {tab:browserTab,browserRoot:win,assignedProfile:browserTab.privateMode?profiles.privateProfile:profiles.normal})
                    }
                    onLoaded: win.pageRevision++
                    Binding { target: pageLoader.item; property: "current"; value: browser.current===pageLoader.browserTab; when: !!pageLoader.item }
                    Binding { target: pageLoader.item; property: "covered"; value: win.panel!=="" || editor.opened || clearDialog.opened || deleteDialog.opened || mainMenu.opened || importDialog.visible || exportDialog.visible; when: !!pageLoader.item }
                }
            }
            Rectangle {
                z: 10; anchors.fill: parent; color: win.palette.window
                visible: win.panel!==""
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 12
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: win.panel; font.pixelSize: 23; Layout.fillWidth: true }
                        Button { text: "Close"; onClicked: win.panel="" }
                    }
                    RowLayout {
                        visible: win.panel==="Bookmarks" || win.panel==="History"
                        Layout.fillWidth: true
                        TextField { id: filter; placeholderText: "Search"; Layout.fillWidth: true; onTextChanged: filterDelay.restart() }
                        Timer { id: filterDelay; interval: 150; onTriggered: win.refresh() }
                        Button { visible: win.panel==="History"; text: "Clear"; onClicked: clearDialog.open() }
                    }
                    RowLayout {
                        visible:win.panel==="Bookmarks"; Layout.fillWidth:true
                        Button { visible: win.panel==="Bookmarks"; text: "New folder"; onClicked: win.editItem(null,true) }
                        Button { visible: win.panel==="Bookmarks"; text: "Import"; onClicked: importDialog.open() }
                        Button { visible: win.panel==="Bookmarks"; text: "Export"; onClicked: exportDialog.open() }
                        Item { Layout.fillWidth:true }
                    }
                    ListView {
                        id: records
                        visible: win.panel==="Bookmarks" || win.panel==="History"
                        Layout.fillWidth: true; Layout.fillHeight: true
                        clip:true; spacing:2
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: records.width; height: 54; color: win.palette.base
                            RowLayout {
                                anchors.fill:parent; anchors.margins:5
                                Button {
                                    id: recordButton
                                    Layout.fillWidth:true; flat:true
                                    text: (modelData.folder?"[Folder] ":"")+(modelData.path||modelData.title||modelData.url)
                                    contentItem: Column {
                                        Label { width:parent.width; elide:Text.ElideRight; text: recordButton.text }
                                        Label { width:parent.width; elide:Text.ElideRight; text:(modelData.url || "")+(modelData.time ? " · "+modelData.time : ""); opacity:0.65; font.pixelSize:12 }
                                    }
                                    onClicked: if(!modelData.folder) { browser.current.navigate(modelData.url); win.panel="" }
                                }
                                Button { visible:win.panel==="Bookmarks"; text:"Edit"; onClicked:win.editItem(modelData,modelData.folder) }
                                Button { text:"Delete"; onClicked: {deleteDialog.record=modelData; deleteDialog.open()} }
                            }
                        }
                    }
                    ListView {
                        id: downloadList
                        visible: win.panel==="Downloads"
                        Layout.fillWidth:true; Layout.fillHeight:true
                        clip:true; spacing:4; model:downloads
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var entry
                            width:downloadList.width; height:86; color:win.palette.base
                            ColumnLayout {
                                anchors.fill:parent; anchors.margins:5
                                Label { Layout.fillWidth:true; elide:Text.ElideRight; text:entry.name+" - "+entry.state+(entry.privateMode?" [Private]":"") }
                                ProgressBar { Layout.fillWidth:true; from:0; to:Math.max(1,entry.total); value:Math.max(0,entry.received); indeterminate:entry.total<0&&!entry.finished }
                                RowLayout {
                                    Layout.fillWidth:true
                                    Label { text:(entry.received/1048576).toFixed(1)+" MiB"; Layout.fillWidth:true }
                                    Button { text:"Pause"; visible:entry.canPause; onClicked:downloads.action(entry.id,"pause") }
                                    Button { text:"Resume"; visible:entry.canResume; onClicked:downloads.action(entry.id,"resume") }
                                    Button { text:"Cancel"; visible:entry.canCancel; onClicked:downloads.action(entry.id,"cancel") }
                                    Button { text:"Folder"; visible:entry.state==="Completed"&&Qt.platform.os!=="ios"; onClicked:downloads.action(entry.id,"folder") }
                                }
                            }
                        }
                    }
                    Button { visible:win.panel==="Downloads"; text:"Clear finished"; onClicked:downloads.clearFinished() }
                    ScrollView {
                        visible: win.panel==="Settings"; Layout.fillWidth:true; Layout.fillHeight:true; clip:true
                        ColumnLayout {
                            width: Math.max(280,parent.width-20); spacing:8
                            Label { text:"Appearance"; font.bold:true }
                            ComboBox { model:["System","Light","Dark"]; currentIndex:model.indexOf(browser.options.theme); onActivated:browser.setOption("theme",currentText) }
                            Label { text:"Search engine URL (%s = search text)" }
                            TextField { Layout.fillWidth:true; text:browser.options.search; onEditingFinished:browser.setOption("search",text) }
                            RowLayout {
                                Button { text:"Google"; onClicked:browser.setOption("search","https://www.google.com/search?q=%s") }
                                Button { text:"DuckDuckGo"; onClicked:browser.setOption("search","https://duckduckgo.com/?q=%s") }
                                Button { text:"Bing"; onClicked:browser.setOption("search","https://www.bing.com/search?q=%s") }
                            }
                            Label { text:"Session and privacy"; font.bold:true }
                            Repeater {
                                model:[["restoreTabs","Restore open tabs"],["restoreSelection","Restore active tab"],["restoreClosed","Restore recently closed tabs"],["restoreSessionCookies","Persist session cookies (desktop)"],["recordHistory","Save regular browsing history"],["adBlock","Block known third-party ad/tracker hosts"],["allowSitePopups","Allow automatic site popups"]]
                                CheckBox { required property var modelData; text:modelData[1]; checked:browser.options[modelData[0]]; enabled:modelData[0]!=="restoreSessionCookies"||Qt.platform.os!=="ios"; onToggled:browser.setOption(modelData[0],checked) }
                            }
                            Label { text:"Additional blocked hostnames (hosts-file format)" }
                            TextArea { Layout.fillWidth:true; Layout.preferredHeight:90; text:browser.options.adHosts; onActiveFocusChanged:if(!activeFocus)browser.setOption("adHosts",text) }
                            Label { text:"Allowlist hostnames (one per line)" }
                            TextArea { Layout.fillWidth:true; Layout.preferredHeight:75; text:browser.options.adExceptions; onActiveFocusChanged:if(!activeFocus)browser.setOption("adExceptions",text) }
                            Label { text:"Keyboard shortcuts"; font.bold:true }
                            Repeater {
                                model:[["newTabKey","New tab"],["privateKey","Private tab"],["closeKey","Close tab"],["reopenKey","Reopen"],["addressKey","Address bar"],["nextKey","Next tab"],["previousKey","Previous tab"],["reloadKey","Reload"],["backKey","Back"],["forwardKey","Forward"],["bookmarkKey","Bookmark"],["historyKey","History"],["downloadsKey","Downloads"],["fullscreenKey","Fullscreen"]]
                                RowLayout {
                                    required property var modelData
                                    Label { text:modelData[1]; Layout.fillWidth:true }
                                    TextField { text:browser.options[modelData[0]]; onEditingFinished:browser.setOption(modelData[0],text) }
                                }
                            }
                            Label { visible:Qt.platform.os!=="ios"; text:"Updates (optional HTTPS release feed)"; font.bold:true }
                            TextField { visible:Qt.platform.os!=="ios"; Layout.fillWidth:true; text:browser.options.updateFeed; onEditingFinished:browser.setOption("updateFeed",text) }
                            RowLayout { visible:Qt.platform.os!=="ios"; Button { text:"Check"; onClicked:updater.check() } Button { text:"Open installer"; enabled:updater.ready; onClicked:updater.install() } }
                            Label { visible:Qt.platform.os!=="ios"; text:updater.status; wrapMode:Text.Wrap; Layout.fillWidth:true }
                        }
                    }
                }
            }
            Label {
                z: 20; anchors.bottom:parent.bottom; anchors.horizontalCenter:parent.horizontalCenter
                visible: text.length>0; text: win.currentPage ? win.currentPage.errorText : ""
                color:"#fff"; padding:8; background:Rectangle {color:"#9e322c";radius:4}
            }
        }
        Label { id:toast; visible:text!==""; padding:5; Layout.fillWidth:true; Timer { id:toastTimer; interval:4500; onTriggered:toast.text="" } }
    }
    Dialog {
        id:editor; modal:true; anchors.centerIn:Overlay.overlay
        property var record:null
        property bool folder:false
        property alias nameText:editName.text
        property var parents:[]
        property int parentId:0
        title:record?"Edit bookmark":folder?"New folder":"Add bookmark"
        standardButtons:Dialog.Ok|Dialog.Cancel
        onOpened:parentBox.currentIndex=Math.max(0,parents.findIndex(x=>Number(x.id)===parentId))
        contentItem:ColumnLayout {
            TextField { id:editName; Layout.fillWidth:true; placeholderText:"Name" }
            ComboBox { id:parentBox; Layout.fillWidth:true; model:editor.parents; textRole:"path"; valueRole:"id" }
        }
        onAccepted: { const p=Number(parentBox.currentValue||0); if(record) browser.editBookmark(record.id,editName.text,p); else if(folder) browser.addFolder(editName.text,p); else if(browser.current) browser.addBookmark(editName.text,browser.current.url,p) }
    }
    Dialog { id:clearDialog; title:"Clear all browsing history?"; modal:true; anchors.centerIn:Overlay.overlay; standardButtons:Dialog.Yes|Dialog.No; onAccepted:browser.clearHistory() }
    Dialog { id:deleteDialog; property var record; title:"Delete this entry and any children?"; modal:true; anchors.centerIn:Overlay.overlay; standardButtons:Dialog.Yes|Dialog.No; onAccepted:if(win.panel==="Bookmarks")browser.removeBookmark(record.id);else browser.removeHistory(record.id) }
    FileDialog { id:importDialog; title:"Import bookmarks"; nameFilters:["Bookmark files (*.html *.htm *.json)"]; onAccepted:browser.importBookmarks(selectedFile) }
    FileDialog { id:exportDialog; title:"Export bookmarks"; fileMode:FileDialog.SaveFile; nameFilters:["HTML (*.html)","JSON (*.json)"]; defaultSuffix:"html"; onAccepted:browser.exportBookmarks(selectedFile) }
    Shortcut { sequence:browser.options.newTabKey; onActivated:{browser.newTab("about:blank",false);win.focusAddress()} }
    Shortcut { sequence:browser.options.privateKey; onActivated:{browser.newTab("about:blank",true);win.focusAddress()} }
    Shortcut { sequence:browser.options.closeKey; onActivated:if(browser.current)browser.closeTab(browser.current.id) }
    Shortcut { sequence:browser.options.reopenKey; onActivated:browser.reopen() }
    Shortcut { sequence:browser.options.addressKey; onActivated:win.focusAddress() }
    Shortcut { sequence:browser.options.nextKey; onActivated:browser.cycle(1) }
    Shortcut { sequence:browser.options.previousKey; onActivated:browser.cycle(-1) }
    Shortcut { sequence:browser.options.reloadKey; onActivated:if(win.currentPage)win.currentPage.reload() }
    Shortcut { sequence:browser.options.backKey; onActivated:if(win.currentPage)win.currentPage.goBack() }
    Shortcut { sequence:browser.options.forwardKey; onActivated:if(win.currentPage)win.currentPage.goForward() }
    Shortcut { sequence:browser.options.bookmarkKey; onActivated:win.editItem(null,false) }
    Shortcut { sequence:browser.options.historyKey; onActivated:win.showPanel("History") }
    Shortcut { sequence:browser.options.downloadsKey; onActivated:win.showPanel("Downloads") }
    Shortcut { sequence:browser.options.fullscreenKey; onActivated:win.toggleFullScreen() }
    Shortcut { sequence:"Escape"; onActivated:if(win.pageFullScreen)win.toggleFullScreen();else win.panel="" }
}
