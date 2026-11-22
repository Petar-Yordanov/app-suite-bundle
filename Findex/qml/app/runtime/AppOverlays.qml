import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../sidebar"
import "../titlebar"
import "../statusbar"
import "../files"
import "../../components/foundation"
import "../../components/theme" as Theme

Item {
    id: root
    anchors.fill: parent

    required property var rootWindow
    required property var sidebarViewModel
    required property var tabsViewModel
    required property var statusBarViewModel
    required property var workspaceViewModel

    property alias sidebarContextMenu: sidebarContextMenu
    property alias breadcrumbContextMenu: breadcrumbContextMenu
    property alias tabContextMenu: tabContextMenu
    property alias fileContextMenu: fileContextMenu

    property alias createMenu: createMenu
    property alias moreActionsMenu: moreActionsMenu
    property alias viewModeMenu: viewModeMenu
    property alias themeMenu: themeMenu
    property alias notificationsPopup: notificationsPopup

    Connections {
        target: root.workspaceViewModel
        function onRemoteTextEditRequested(path) {
            const document = root.workspaceViewModel.loadRemoteDocument(path)
            // Never open an empty editor when Docker copy or UTF-8 decoding failed.
            if (!document.ok) return
            textEditor.path = path
            textEditorContents.text = document.text
            textEditor.open()
        }
    }

    Dialog {
        id: textEditor
        property string path: ""
        parent: root
        anchors.centerIn: parent
        width: Math.min(850, root.width - 40)
        height: Math.min(640, root.height - 60)
        modal: true
        title: "Edit container file: " + path
        standardButtons: Dialog.Save | Dialog.Cancel
        onAccepted: {
            if (!root.workspaceViewModel.writeRemoteText(path, textEditorContents.text)) open()
        }
        contentItem: Rectangle {
            color: Theme.AppTheme.surface2
            ScrollView {
                anchors.fill: parent
                TextArea {
                    id: textEditorContents
                    font.family: "monospace"
                    font.pixelSize: 13
                    color: Theme.AppTheme.text
                    wrapMode: TextEdit.NoWrap
                    selectByMouse: true
                }
            }
        }
    }

    SidebarContextMenu {
        id: sidebarContextMenu
        viewModel: root.sidebarViewModel
    }

    StyledMenu {
        id: breadcrumbContextMenu
        darkTheme: Theme.AppTheme.isDark

        StyledMenuItem {
            text: "Copy path"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: breadcrumbContextMenu.close()
        }
    }

    TabContextMenu {
        id: tabContextMenu
        viewModel: root.tabsViewModel
    }

    FileContextMenu {
        id: fileContextMenu
        viewModel: root.workspaceViewModel
        onAdministratorRequested: function(path) { adminSource.text=path;adminDestination.text="";adminAction.currentIndex=0;adminDialog.open() }
    }

    Dialog {
        id: adminDialog
        parent: root
        anchors.centerIn: parent
        width: Math.min(600,root.width-32)
        modal: true
        title: "One-time administrator operation"
        standardButtons: Dialog.Ok | Dialog.Cancel
        onOpened: { adminConfirm.checked=false; adminTypedPath.text="" }
        onAccepted: {
            if(adminAction.currentValue==="remove-tree"&&(!adminConfirm.checked||adminTypedPath.text!==adminSource.text)) return
            root.workspaceViewModel.administratorOperation(adminAction.currentValue,adminSource.text,adminDestination.text)
        }
        ColumnLayout {
            anchors.fill: parent
            ComboBox {
                id: adminAction; Layout.fillWidth: true
                textRole:"label";valueRole:"operation"
                model:[{label:"Copy file or directory",operation:"copy"},{label:"Rename / move on same volume",operation:"rename"},
                    {label:"Create file",operation:"create-file"},{label:"Create folder",operation:"create-directory"},
                    {label:"Permanently remove file or empty folder",operation:"remove"},
                    {label:"Permanently remove directory tree",operation:"remove-tree"}]
                onCurrentIndexChanged: if(adminConfirm)adminConfirm.checked=false
            }
            Label {text:"Absolute source or new item path"}
            TextField {id:adminSource;Layout.fillWidth:true;selectByMouse:true}
            Label {visible:adminAction.currentValue==="copy"||adminAction.currentValue==="rename";text:"Absolute destination path (including filename)"}
            TextField {id:adminDestination;visible:adminAction.currentValue==="copy"||adminAction.currentValue==="rename";Layout.fillWidth:true;selectByMouse:true}
            Label {Layout.fillWidth:true;wrapMode:Text.WordWrap;text:adminAction.currentValue==="remove-tree"?"Warning: all files and subdirectories will be permanently deleted. Only use trusted directories that are not being modified by other processes.":adminAction.currentValue==="remove"?"Warning: this removes the item permanently; it is not moved to the trash. Only files, links and empty folders are permitted.":"The OS will request administrator authorization for this one action. Existing destinations will not be overwritten."}
            CheckBox {id:adminConfirm;visible:adminAction.currentValue==="remove-tree";text:"I understand that this permanently deletes the entire directory."}
            TextField {id:adminTypedPath;visible:adminAction.currentValue==="remove-tree";Layout.fillWidth:true;placeholderText:"Retype the full source path to confirm"}
        }
    }

    StyledMenu {
        id: createMenu
        darkTheme: Theme.AppTheme.isDark

        StyledMenuItem {
            text:"Create as administrator..."
            darkTheme:Theme.AppTheme.isDark
            enabled:!root.workspaceViewModel.currentDirectoryPath.startsWith("container://")
            onTriggered:{createMenu.close();const dir=root.workspaceViewModel.currentDirectoryPath;adminSource.text=dir+(dir.endsWith("/")?"":"/")+"New folder";adminDestination.text="";adminAction.currentIndex=3;adminDialog.open()}
        }

        StyledMenuItem {
            text: "New folder"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.createFolder()
                createMenu.close()
            }
        }

        StyledMenuItem {
            text: "New file"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.createFile()
                createMenu.close()
            }
        }
    }

    StyledMenu {
        id: viewModeMenu
        darkTheme: Theme.AppTheme.isDark

        StyledMenuItem {
            text: "Details"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyViewMode("Details")
                viewModeMenu.close()
            }
        }

        StyledMenuItem {
            text: "Tiles"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyViewMode("Tiles")
                viewModeMenu.close()
            }
        }

        StyledMenuItem {
            text: "Compact"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyViewMode("Compact")
                viewModeMenu.close()
            }
        }

        StyledMenuItem {
            text: "Large icons"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyViewMode("Large icons")
                viewModeMenu.close()
            }
        }
    }

    StyledMenu {
        id: themeMenu
        darkTheme: Theme.AppTheme.isDark

        StyledMenuItem {
            text: "Dark"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyTheme("Dark")
                themeMenu.close()
            }
        }

        StyledMenuItem {
            text: "Light"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.applyTheme("Light")
                themeMenu.close()
            }
        }
    }

    StyledMenu {
        id: moreActionsMenu
        darkTheme: Theme.AppTheme.isDark

        StyledMenuItem {
            text: "Compress"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.compressSelection()
                moreActionsMenu.close()
            }
        }

        StyledMenuItem {
            text: "Extract here"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.extractSelection()
                moreActionsMenu.close()
            }
        }

        StyledMenuSeparator {}

        StyledMenuItem {
            text: "Select all"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.selectAll()
                moreActionsMenu.close()
            }
        }

        StyledMenuItem {
            text: root.rootWindow.commandBarVm && root.rootWindow.commandBarVm.showHiddenFiles
                  ? "Hide hidden files"
                  : "Show hidden files"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.toggleHiddenFiles()
                moreActionsMenu.close()
            }
        }

        StyledMenuSeparator {}

        StyledMenuItem {
            text: "Open terminal here"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: { moreActionsMenu.close(); root.workspaceViewModel.openSuiteTerminal() }
        }

        StyledMenuItem {
            text: "Updates"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: { moreActionsMenu.close(); updatesDialog.open() }
        }

        StyledMenuItem {
            text: "Properties"
            darkTheme: Theme.AppTheme.isDark
            onTriggered: {
                if (root.rootWindow.commandBarVm)
                    root.rootWindow.commandBarVm.showProperties()
                moreActionsMenu.close()
            }
        }
    }

    Dialog {
        id: updatesDialog
        parent: root
        anchors.centerIn: parent
        width: Math.min(520,root.width-40)
        title: "Updates"
        modal: true
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            Label { text: appUpdater.status; Layout.fillWidth:true; wrapMode: Text.WordWrap }
            TextField { id: updateFeed; Layout.fillWidth:true; text:appUpdater.feed; placeholderText:"HTTPS update manifest (empty disables updates)" }
            RowLayout {
                Button { text:"Save feed"; onClicked:appUpdater.feed=updateFeed.text }
                Button { text:"Check now"; onClicked:appUpdater.check() }
                Button { text:"Open installer"; enabled:appUpdater.ready; onClicked:appUpdater.install() }
            }
        }
    }

    NotificationsPopup {
        id: notificationsPopup
        viewModel: root.statusBarViewModel
    }

    Item {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 14
        anchors.bottomMargin: 42
        z: 60000
        width: 380
        height: parent.height
        clip: false

        Column {
            id: toastColumn
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Theme.Metrics.spacingSm

            Repeater {
                model: root.statusBarViewModel ? root.statusBarViewModel.toastNotifications : []

                delegate: NotificationCard {
                    required property var modelData

                    width: 360
                    darkTheme: Theme.AppTheme.isDark

                    notificationId: modelData.id
                    title: modelData.title || ""
                    details: modelData.details || ""
                    kind: modelData.kind || "info"
                    progress: modelData.progress === undefined ? -1 : modelData.progress
                    autoClose: modelData.autoClose === undefined ? true : !!modelData.autoClose
                    done: !!modelData.done

                    onCloseRequested: function(id) {
                        if (root.statusBarViewModel)
                            root.statusBarViewModel.dismissToast(id)
                    }
                }
            }
        }
    }
}
