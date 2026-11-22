import QtQuick
import QtQuick.Controls
import QtWebEngine

WebEngineView {
    id: page
    required property var tab
    required property var browserRoot
    property bool current: false
    property bool covered: false
    property string errorText: ""
    property bool initialized: false
    required property var assignedProfile
    profile: assignedProfile
    visible: current && !covered
    settings.fullScreenSupportEnabled: true
    settings.screenCaptureEnabled: true
    settings.javascriptCanOpenWindows: browser.options.allowSitePopups
    settings.allowRunningInsecureContent: false
    settings.localContentCanAccessRemoteUrls: false
    Component.onCompleted: { const initial=tab.url; initialized=true; url=initial }
    Connections {
        target: page.tab
        function onLoadRequested(u) { page.errorText = ""; if (page.url.toString()===u.toString()) page.reload(); else page.url = u }
    }
    onUrlChanged: if (tab && initialized) { tab.report(url, title); if (!loading && !errorText) sameDocumentTimer.restart() }
    onTitleChanged: if (tab && initialized) tab.report(url, title)
    onLoadingChanged: function(request) {
        sameDocumentTimer.stop()
        if (request.status === WebEngineView.LoadSucceededStatus) {
            errorText = ""
            if(request.url.toString()===url.toString()) tab.commit(url, title)
        } else if (request.status === WebEngineView.LoadFailedStatus) {
            errorText = request.errorString
        }
    }
    Timer { id: sameDocumentTimer; interval: 150; onTriggered: if (!page.loading && !page.errorText) page.tab.sameDocument(page.url,page.title) }
    onCurrentChanged: if (current) lifecycleState = WebEngineView.Active
    Timer {
        interval: 30000
        repeat: true
        running: !page.current
        onTriggered: if (page.recommendedState !== page.lifecycleState)
                         page.lifecycleState = page.recommendedState
    }
    onNewWindowRequested: function(request) {
        if (!current || (!request.userInitiated && !browser.options.allowSitePopups)) return
        const t = browser.newTab("about:blank", tab.privateMode)
        if (t) {
            const target = browserRoot.pageFor(t.id)
            if (target) request.openIn(target)
            else t.navigate(request.requestedUrl)
        }
    }
    onNavigationRequested: function(request) {
        const u = request.url.toString()
        if (request.isMainFrame && !/^(https?:|about:blank|blob:|data:)/i.test(u))
            request.reject()
    }
    onCertificateError: function(error) { error.rejectCertificate() }
    onRenderProcessTerminated: function(status,exitCode) { errorText = "Web page process stopped. Reload to retry." }
    onFullScreenRequested: function(request) {
        if (current) {
            request.accept()
            browserRoot.setFullScreen(request.toggleOn)
        } else request.reject()
    }
    function exitPageFullScreen() { triggerWebAction(WebEngineView.ExitFullScreen) }
    onPermissionRequested: function(permission) {
        if (!current) { permission.deny(); return }
        if (permissionDialog.pending) permissionDialog.pending.deny()
        permissionDialog.pending = permission
        permissionDialog.open()
    }
    onDesktopMediaRequested: function(request) {
        if (!current) { request.cancel(); return }
        if (shareDialog.pending) shareDialog.pending.cancel()
        shareDialog.pending=request
        shareDialog.open()
    }
    Dialog {
        id:shareDialog
        property var pending:null
        modal:true
        anchors.centerIn:Overlay.overlay
        title:"Choose a screen or window to share"
        standardButtons:Dialog.Cancel
        contentItem: Column {
            spacing:5
            Label { text:page.url.toString(); width:320; elide:Text.ElideRight }
            ComboBox { id:shareType; model:["Screens","Windows"]; width:320 }
            ListView {
                width:320;height:220;clip:true
                model:shareDialog.pending?(shareType.currentIndex===0?shareDialog.pending.screensModel:shareDialog.pending.windowsModel):null
                delegate:Button {
                    required property int index
                    required property string display
                    width:320;text:display
                    onClicked: {
                        const request=shareDialog.pending
                        if(!request)return
                        const m=shareType.currentIndex===0?request.screensModel:request.windowsModel
                        if(shareType.currentIndex===0)request.selectScreen(m.index(index,0))
                        else request.selectWindow(m.index(index,0))
                        shareDialog.pending=null
                        shareDialog.close()
                    }
                }
            }
        }
        onClosed:{if(pending)pending.cancel();pending=null}
    }
    Dialog {
        id: permissionDialog
        property var pending: null
        modal: true
        anchors.centerIn: Overlay.overlay
        title: "Website permission"
        standardButtons: Dialog.Ok | Dialog.Cancel
        contentItem: Label {
            width: 320
            wrapMode: Text.Wrap
            text: permissionDialog.pending ?
                String(permissionDialog.pending.origin) + "\n\nAllow " +
                permissionName(permissionDialog.pending.permissionType) + "?" : ""
            function permissionName(t) {
                switch(t) {
                case WebEnginePermission.Geolocation: return "location access"
                case WebEnginePermission.MediaAudioCapture: return "microphone access"
                case WebEnginePermission.MediaVideoCapture: return "camera access"
                case WebEnginePermission.MediaAudioVideoCapture: return "microphone and camera access"
                case WebEnginePermission.DesktopVideoCapture: return "screen sharing"
                case WebEnginePermission.DesktopAudioVideoCapture: return "screen and audio sharing"
                case WebEnginePermission.Notifications: return "notifications"
                case WebEnginePermission.ClipboardReadWrite: return "clipboard access"
                default: return "website capability (" + t + ")"
                }
            }
        }
        onAccepted: { if (pending) pending.grant(); pending = null }
        onRejected: { if (pending) pending.deny(); pending = null }
        onClosed: { if (pending) pending.deny(); pending = null }
    }
    onContextMenuRequested: function(request) {
        request.accepted = true
        contextMenu.link = request.linkUrl
        contextMenu.popup()
    }
    Menu {
        id: contextMenu
        property url link
        MenuItem { text: "Open link in new tab"; enabled: contextMenu.link.toString().length > 0; onTriggered: browser.newTab(contextMenu.link, tab.privateMode) }
        MenuItem { text: "Back"; enabled: page.canGoBack; onTriggered: page.goBack() }
        MenuItem { text: "Reload"; onTriggered: page.reload() }
        MenuItem { text: "Copy"; onTriggered: page.triggerWebAction(WebEngineView.Copy) }
        MenuItem { text: "Cut"; onTriggered: page.triggerWebAction(WebEngineView.Cut) }
        MenuItem { text: "Paste"; onTriggered: page.triggerWebAction(WebEngineView.Paste) }
        MenuItem { text: "Select all"; onTriggered: page.triggerWebAction(WebEngineView.SelectAll) }
        MenuItem { text: "Save image"; onTriggered: page.triggerWebAction(WebEngineView.DownloadImageToDisk) }
        MenuItem { text: "Save link"; enabled: contextMenu.link.toString().length > 0; onTriggered: page.triggerWebAction(WebEngineView.DownloadLinkToDisk) }
    }
}
