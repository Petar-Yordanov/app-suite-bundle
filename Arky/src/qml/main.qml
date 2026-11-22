import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs as NativeDialogs
import "theme" as Theme
import "ui" as Ui

ApplicationWindow {
    id: win
    visible: true
    width: 1280
    height: 820
    minimumWidth: 1000
    minimumHeight: 620
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    title: backend.archiveName === "No archive open" ? "Arky" : backend.archiveName + " - Arky"
    color: Theme.AppTheme.bg
    font.family: "Segoe UI"
    font.pixelSize: Theme.Typography.bodyLg
    palette.window: Theme.AppTheme.bg
    palette.windowText: Theme.AppTheme.text
    palette.text: Theme.AppTheme.text
    palette.buttonText: Theme.AppTheme.text
    palette.base: Theme.AppTheme.surface3
    palette.button: Theme.AppTheme.surface
    palette.highlight: Theme.AppTheme.selected
    palette.highlightedText: Theme.AppTheme.text

    readonly property color appBg: Theme.AppTheme.bg
    readonly property color chromeBg: Theme.AppTheme.titleBg
    readonly property color chromeBg2: Theme.AppTheme.surface
    readonly property color panelBg: Theme.AppTheme.surface
    readonly property color panelBg2: Theme.AppTheme.surface2
    readonly property color frameColor: Theme.AppTheme.border
    readonly property color borderSoft: Theme.AppTheme.borderSoft
    readonly property color textColor: Theme.AppTheme.text
    readonly property color mutedText: Theme.AppTheme.muted
    readonly property color accent: Theme.AppTheme.accent
    readonly property color accentSoft: Theme.AppTheme.selectedSoft
    readonly property color contentBg: Theme.AppTheme.surface3
    property string alertText: ""
    property bool showPassword: false
    property bool extractSelection: false
    property int extractionConflicts: 0
    property bool treeView: false
    Binding { target: Theme.AppTheme; property: "mode"; value: backend.theme }
    Binding { target: Theme.AppTheme; property: "systemDark"; value: Qt.styleHints.colorScheme === Qt.Dark }

    function extensionForFilter(i) {
        var extensions = ["zip", "7z", "tar", "tar.gz", "tar.xz", "tar.zst"]
        return i >= 0 && i < extensions.length ? extensions[i] : "zip"
    }
    function beginCreateArchiveFromFiles() { createDialog.close(); filesDialog.open() }
    function beginCreateArchiveFromFolder() { createDialog.close(); inputFolderDialog.open() }
    function openExtractDialog(selectedOnly) { extractSelection = selectedOnly; extractFolderDialog.open() }
    function removeSelection() { if (backend.editable && backend.selectedCount > 0 && !backend.busy) removeConfirm.open() }
    function toggleMaximize() {
        win.visibility = win.visibility === Window.Maximized ? Window.Windowed : Window.Maximized
    }

    onClosing: function(close) {
        if (backend.busy) {
            backend.cancel()
            close.accepted = false
            alertText = "Cancelling current operation. Close the window when it finishes."
        }
    }

    Shortcut { sequence: backend.shortcuts.open; onActivated: openDialog.open() }
    Shortcut { sequence: backend.shortcuts.copy; enabled: !filterField.activeFocus && !passwordField.activeFocus; onActivated: backend.copySelection(passwordField.text) }
    Shortcut { sequence: backend.shortcuts.paste; enabled: !filterField.activeFocus && !passwordField.activeFocus && backend.editable && !backend.busy; onActivated: backend.pasteFiles() }
    Shortcut {
        sequence: backend.shortcuts.rename
        enabled: backend.editable && backend.selectedCount === 1 && !backend.busy
        onActivated: { renameField.text = backend.selectedName; renameDialog.open() }
    }
    Shortcut { sequence: backend.shortcuts.remove; enabled: backend.editable && backend.selectedCount > 0 && !backend.busy; onActivated: removeSelection() }
    Shortcut { sequence: backend.shortcuts.selectAll; enabled: !filterField.activeFocus && !passwordField.activeFocus; onActivated: backend.selectAll() }

    Shortcut { sequence: backend.shortcuts.extract; enabled: backend.archivePath !== "" && !backend.loading; onActivated: openExtractDialog(false) }
    Shortcut { sequence: backend.shortcuts.create; onActivated: createDialog.open() }

    Connections {
        target: backend
        function onTabsChanged() { passwordField.text = "" }
        function onRequestTrash(archive) { trashPath.text = archive; rememberTrash.checked = false; trashDialog.open() }
        function onRequestConflict(path) { conflictPath.text = path; conflictDialog.open() }
        function onConflictFinished() { conflictDialog.close() }
        function onFailed(reason) { alertText = reason }
        function onCompleted(message) { alertText = message }
        function onRequestSaveArchive() { saveDialog.open() }
        function onPreviewReady(name, content, truncated) {
            previewTitle.text = name + (truncated ? " — preview truncated at 128 KiB" : "")
            previewText.text = content
            previewPopup.open()
        }
    }

    Ui.FMenu {
        id: fileMenu
        Ui.FMenuItem { text: "Open archive..."; onTriggered: openDialog.open() }
        Ui.FMenuItem { text: "New archive..."; onTriggered: createDialog.open() }
        Ui.FMenuItem { text: "Add files..."; enabled: backend.editable && !backend.busy; onTriggered: addFilesDialog.open() }
        Ui.FMenuItem { text: "Add folder..."; enabled: backend.editable && !backend.busy; onTriggered: addFolderDialog.open() }
        Ui.FMenuSeparator {}
        Ui.FMenuItem { text: "Extract here"; enabled: backend.archivePath !== "" && !backend.loading; onTriggered: backend.extractHere(passwordField.text, extractionConflicts) }
        Ui.FMenuItem { text: "Extract all..."; enabled: backend.archivePath !== "" && !backend.loading; onTriggered: openExtractDialog(false) }
        Ui.FMenuItem { text: "Extract selected..."; enabled: backend.selectedCount > 0 && !backend.loading; onTriggered: openExtractDialog(true) }
        Ui.FMenuSeparator {}
        Ui.FMenuItem { text: "Exit"; onTriggered: win.close() }
    }
    Ui.FMenu {
        id: commandsMenu
        Ui.FMenuItem { text: "Test archive"; enabled: backend.archivePath !== "" && !backend.busy; onTriggered: backend.testArchive(passwordField.text) }
        Ui.FMenuItem { text: "Open with default application / archive"; enabled: backend.selectedCount === 1 && !backend.busy; onTriggered: backend.openSelected(passwordField.text) }
        Ui.FMenuItem { text: "Preview"; enabled: backend.selectedCount === 1 && !backend.busy; onTriggered: backend.previewSelected(passwordField.text) }
        Ui.FMenuItem { text: "Rename"; enabled: backend.editable && backend.selectedCount === 1 && !backend.busy; onTriggered: { renameField.text = backend.selectedName; renameDialog.open() } }
        Ui.FMenuItem { text: "Move"; enabled: backend.editable && backend.selectedCount > 0 && !backend.busy; onTriggered: moveDialog.open() }
        Ui.FMenuItem { text: "Replace..."; enabled: backend.editable && backend.selectedCount === 1 && !backend.busy; onTriggered: replaceDialog.open() }
        Ui.FMenuItem { text: "Remove"; enabled: backend.editable && backend.selectedCount > 0 && !backend.busy; onTriggered: removeSelection() }
        Ui.FMenuSeparator {}
        Ui.FMenuItem { text: "New folder"; enabled: backend.editable && !backend.busy; onTriggered: folderDialog.open() }
        Ui.FMenuItem { text: "Paste files"; enabled: backend.editable && !backend.busy; onTriggered: backend.pasteFiles() }
    }
    Ui.FMenu {
        id: toolsMenu
        Ui.FMenuItem { text: "Copy metadata"; enabled: backend.selectedCount > 0; onTriggered: backend.copyEntryInfo() }
        Ui.FMenuItem { text: "Copy files"; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.copySelection(passwordField.text) }
        Ui.FMenuItem { text: "Copy virtual"; visible: Qt.platform.os === "windows"; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.copyVirtualSelection(passwordField.text) }
        Ui.FMenuItem { text: "Prepare file URLs"; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.prepareDrag(passwordField.text) }
        Ui.FMenuItem { text: "Open prepared files"; enabled: backend.prepared; onTriggered: backend.showExportFolder() }
    }
    Ui.FMenu {
        id: optionsMenu
        Ui.FMenuItem { text: "Light appearance"; iconName: "sun"; onTriggered: backend.theme = "Light" }
        Ui.FMenuItem { text: "Dark appearance"; iconName: "moon"; onTriggered: backend.theme = "Dark" }
        Ui.FMenuItem { text: "System appearance"; onTriggered: backend.theme = "System" }
        Ui.FMenuItem { text: "Settings..."; onTriggered: settingsDialog.open() }
    }
    Ui.FMenu {
        id: helpMenu
        Ui.FMenuItem { text: "About Arky"; iconName: "info"; onTriggered: aboutDialog.open() }
    }

    NativeDialogs.FileDialog {
        id: openDialog
        title: "Open archive"
        fileMode: NativeDialogs.FileDialog.OpenFile
        nameFilters: ["Archives (*.zip *.zipx *.7z *.rar *.tar *.gz *.xz *.bz2 *.zst *.cab *.iso *.cpio *.xar *.lha *.lzh *.warc *.001 *.ar *.rar5)", "All files (*)"]
        onAccepted: backend.openUrl(selectedFile, passwordField.text)
    }
    NativeDialogs.FileDialog {
        id: filesDialog
        title: "Choose files to compress"
        fileMode: NativeDialogs.FileDialog.OpenFiles
        nameFilters: ["All files (*)"]
        onAccepted: backend.beginCreate(selectedFiles)
    }
    NativeDialogs.FolderDialog { id: inputFolderDialog; title: "Choose folder to compress"; onAccepted: backend.beginCreateFolder(selectedFolder) }
    NativeDialogs.FileDialog {
        id: saveDialog
        title: "Save new archive"
        fileMode: NativeDialogs.FileDialog.SaveFile
        defaultSuffix: ""
        nameFilters: [archiveFormat.currentText]
        onAccepted: backend.saveNewArchive(selectedFile, extensionForFilter(archiveFormat.currentIndex),
                                           compressionLevel.currentIndex === 0 ? -1 : compressionLevel.currentIndex === 1 ? 0 : compressionLevel.currentIndex === 2 ? 1 : compressionLevel.currentIndex === 3 ? 5 : 9,
                                           compressionMethod.currentText)
    }
    NativeDialogs.FolderDialog { id: extractFolderDialog; title: "Extract to folder"; onAccepted: backend.extractTo(selectedFolder, extractSelection, passwordField.text, extractionConflicts) }
    NativeDialogs.FileDialog { id: addFilesDialog; title: "Add files to the open archive"; fileMode: NativeDialogs.FileDialog.OpenFiles; onAccepted: backend.appendFiles(selectedFiles, passwordField.text) }
    NativeDialogs.FolderDialog { id: addFolderDialog; title: "Add folder to the current archive directory"; onAccepted: backend.addFolder(selectedFolder, passwordField.text) }
    NativeDialogs.FileDialog { id: replaceDialog; title: "Choose replacement file for selected archive entry"; fileMode: NativeDialogs.FileDialog.OpenFile; onAccepted: backend.replaceSelected(selectedFile, passwordField.text) }

    Ui.FDialog {
        id: aboutDialog
        title: "About Arky"
        modal: true
        width: 430
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 10
            Label {
                text: "Arky"
                color: textColor
                font.pixelSize: 22
                font.bold: true
                Layout.fillWidth: true
            }
            Label {
                text: "Arky is a crossplatform archive manager supporting a range of formats."
                color: textColor
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: "Version " + Qt.application.version
                color: mutedText
                Layout.fillWidth: true
            }
        }
    }

    Ui.FDialog {
        id: createDialog
        title: "New archive"
        modal: true
        width: 470
        anchors.centerIn: parent
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            spacing: 10
            Label { text: "Choose how the new archive should be created."; color: textColor }
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 8
                Label { text: "Format"; color: textColor }
                Ui.FComboBox {
                    id: archiveFormat
                    Layout.fillWidth: true
                    model: ["ZIP (*.zip)", "7-Zip (*.7z)", "TAR (*.tar)", "GZip TAR (*.tar.gz)", "XZ TAR (*.tar.xz)", "Zstd TAR (*.tar.zst)"]
                    currentIndex: 0
                    onActivated: { compressionMethod.currentIndex = 0; compressionLevel.currentIndex = 0 }
                }
                Label { text: "Method"; color: textColor }
                Ui.FComboBox { id: compressionMethod; Layout.fillWidth: true; model: backend.compressionMethods(extensionForFilter(archiveFormat.currentIndex)); currentIndex: 0 }
                Label { text: "Level"; color: textColor }
                Ui.FComboBox { id: compressionLevel; Layout.fillWidth: true; enabled: backend.outputCapabilities(extensionForFilter(archiveFormat.currentIndex)).supportsCompressionLevel; model: ["Default", "Store (0)", "Fast (1)", "Balanced (5)", "Best (9)"]; currentIndex: 0 }

            }
            Rectangle { Layout.fillWidth: true; height: 1; color: frameColor }
            RowLayout {
                Layout.fillWidth: true
                Ui.FButton { label: "Choose files..."; iconName: "add"; primary: true; onTriggered: beginCreateArchiveFromFiles() }
                Ui.FButton { label: "Choose folder..."; iconName: "folder"; onTriggered: beginCreateArchiveFromFolder() }
                Item { Layout.fillWidth: true }
            }
            Label { Layout.fillWidth: true; wrapMode: Text.WordWrap; color: mutedText; text: "Archives are created without encryption. Compression options depend on the format." }
        }
    }
    Ui.FDialog {
        id: folderDialog
        title: "New folder"
        modal: true
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: backend.newFolder(newFolderName.text, passwordField.text)
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Folder name inside current archive directory"
                color: textColor
            }
            Ui.FTextField {
                id: newFolderName
                Layout.fillWidth: true
                placeholderText: "New Folder"
            }
        }
    }
    Ui.FDialog {
        id: renameDialog
        title: "Rename entry"
        modal: true
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: backend.renameSelected(renameField.text, passwordField.text)
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "New filename or folder name"
                color: textColor
            }
            Ui.FTextField {
                id: renameField
                Layout.fillWidth: true
            }
        }
    }
    Ui.FDialog {
        id: moveDialog
        title: "Move selected entries"
        modal: true
        anchors.centerIn: parent
        width: 460
        standardButtons: Dialog.Ok | Dialog.Cancel

        onAccepted: backend.moveSelected(moveField.text, passwordField.text)
        contentItem: ColumnLayout {
            spacing: 8
            Label {
                text: "Destination folder inside archive (leave blank for root)"
                color: textColor
            }
            Ui.FTextField {
                id: moveField
                Layout.fillWidth: true
                placeholderText: "documents/2026"
            }
        }
    }
    Ui.FDialog {
        id: removeConfirm
        title: "Remove selected entries?"
        modal: true
        anchors.centerIn: parent
        width: 400
        standardButtons: Dialog.Yes | Dialog.Cancel

        onAccepted: backend.removeSelected(passwordField.text)
        contentItem: Label { width: parent ? parent.width : 360; wrapMode: Text.WordWrap; color: textColor; text: "The selected ZIP entries will be removed in a libzip transaction. The original stays intact if the transaction fails." }
    }
    Ui.FDialog {
        id: previewPopup
        title: "Entry preview"
        modal: true
        width: Math.min(win.width - 100, 820)
        height: Math.min(win.height - 100, 650)
        anchors.centerIn: parent
        standardButtons: Dialog.Close

        contentItem: ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { id: previewTitle; text: "Preview"; Layout.fillWidth: true; font.bold: true; color: textColor; elide: Text.ElideMiddle }
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                ScrollBar.vertical: Ui.FScrollBar { orientation: Qt.Vertical }
                ScrollBar.horizontal: Ui.FScrollBar { orientation: Qt.Horizontal }
                Ui.FTextArea {
                    id: previewText
                    readOnly: true
                    wrapMode: TextEdit.NoWrap
                    font.family: "Consolas"
                    font.pixelSize: 12
                    selectByMouse: true
                }
            }
        }
    }

    Ui.FDialog {
        id: propertiesDialog
        title: "Archive properties"
        width: 550
        standardButtons: Dialog.Close
        contentItem: Label { id: archiveProperties; wrapMode: Text.WrapAnywhere; color: textColor }
    }
    Ui.FDialog {
        id: trashDialog
        title: "Move original archive to Trash?"
        width: 500
        standardButtons: Dialog.Yes | Dialog.Cancel
        onRejected: backend.confirmTrash(false, rememberTrash.checked)
        onAccepted: backend.confirmTrash(true, rememberTrash.checked)
        contentItem: ColumnLayout {
            Label { id: trashPath; color: textColor; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            Ui.FCheckBox { id: rememberTrash; text: "Remember my choice" }
            Label { text: "The extraction succeeded. The original will be moved to the system Trash, not permanently deleted."; wrapMode: Text.WordWrap; color: mutedText; Layout.fillWidth: true }
        }
        footer: RowLayout {
            Item { Layout.fillWidth: true }
            Ui.FButton { label: "No"; onTriggered: trashDialog.reject() }
            Ui.FButton { label: "Yes"; primary: true; onTriggered: trashDialog.accept() }
        }
    }
    Ui.FDialog {
        id: conflictDialog
        title: "Destination file already exists"
        width: 560
        closePolicy: Popup.NoAutoClose
        standardButtons: Dialog.Cancel
        onRejected: backend.resolveConflict(0)
        contentItem: ColumnLayout {
            Label { id: conflictPath; color: textColor; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
            RowLayout {
                Ui.FButton { label: "Skip"; onTriggered: { backend.resolveConflict(1); conflictDialog.close() } }
                Ui.FButton { label: "Rename"; onTriggered: { backend.resolveConflict(2); conflictDialog.close() } }
                Ui.FButton { label: "Overwrite"; onTriggered: { backend.resolveConflict(3); conflictDialog.close() } }
                Ui.FButton { label: "Cancel extraction"; onTriggered: { backend.resolveConflict(0); conflictDialog.close() } }
            }
        }
        footer: Item { implicitHeight: 0 }
    }
    Ui.FDialog {
        id: settingsDialog
        title: "Settings"
        width: 600
        standardButtons: Dialog.Close
        contentItem: ColumnLayout {
            Label { text: "After complete extraction"; color: textColor }
            Ui.FComboBox { model: ["Ask every time", "Move original to Trash", "Never delete"]; currentIndex: backend.deletePolicy; onActivated: backend.deletePolicy = currentIndex; Layout.fillWidth: true }
            Label { text: "Compression threads (XZ / Zstandard)"; color: textColor }
            SpinBox { from: 1; to: 16; value: backend.compressionThreads; onValueModified: backend.compressionThreads = value }
            Label { text: "Keyboard shortcuts"; color: textColor }
            GridLayout {
                columns: 2
                Repeater {
                    model: Object.keys(backend.shortcuts)
                    delegate: RowLayout {
                        required property string modelData
                        Label { text: modelData; color: textColor }
                        Ui.FTextField { text: backend.shortcuts[modelData]; onEditingFinished: backend.setShortcut(modelData, text); Layout.preferredWidth: 120 }
                    }
                }
            }
            Label { text: "Trusted HTTPS update manifest (optional)"; color: textColor }
            Ui.FTextField { id: feedField; text: backend.updateUrl; Layout.fillWidth: true; placeholderText: "https://.../release.json"; onEditingFinished: backend.updateUrl = text }
            Label { text: updater.status; color: mutedText; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            RowLayout {
                Ui.FButton { label: "Check updates"; onTriggered: { backend.updateUrl = feedField.text; updater.check() } }
                Ui.FButton { label: "Install downloaded update"; enabled: updater.ready; onTriggered: updater.install() }
            }
        }
    }

    DropArea {
        id: dropReceiver
        anchors.fill: parent
        z: 3
        onDropped: function(drop) {
            if (drop.hasUrls && !backend.busy) {
                backend.droppedUrls(drop.urls)
                drop.acceptProposedAction()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: appBg
        border.color: win.frameColor
        border.width: 1

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: chromeBg
                border.color: borderSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 0
                    spacing: 8
                    Rectangle {
                        width: 22; height: 22; radius: 6
                        color: accentSoft
                        border.color: win.frameColor
                        Ui.FIcon { anchors.centerIn: parent; name: "zip"; iconSize: 17 }
                    }
                    Label { text: "Arky"; color: textColor; font.pixelSize: 14; font.bold: true }
                    Label { text: backend.archiveName !== "No archive open" ? "• " + backend.archiveName : ""; color: mutedText; Layout.fillWidth: true; elide: Text.ElideMiddle }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        color: "transparent"
                        MouseArea {
                            anchors.fill: parent
                            acceptedButtons: Qt.LeftButton
                            onPressed: function(mouse) { if (mouse.button === Qt.LeftButton) win.startSystemMove() }
                            onDoubleClicked: toggleMaximize()
                        }
                    }
                    Ui.FWindowButton {
                        iconName: "minimize"
                        onClicked: win.showMinimized()
                    }
                    Ui.FWindowButton {
                        iconName: win.visibility === Window.Maximized ? "filter-none" : "check-box-outline-blank"
                        onClicked: toggleMaximize()
                    }
                    Ui.FWindowButton {
                        iconName: "close"
                        destructive: true
                        onClicked: win.close()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: chromeBg2
                border.color: borderSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 18

                    Ui.FMenuTrigger { id: fileTrigger; text: "File"; onTriggered: fileMenu.popupBelow(fileTrigger) }
                    Ui.FMenuTrigger { id: commandsTrigger; text: "Commands"; onTriggered: commandsMenu.popupBelow(commandsTrigger) }
                    Ui.FMenuTrigger { id: toolsTrigger; text: "Tools"; onTriggered: toolsMenu.popupBelow(toolsTrigger) }
                    Ui.FMenuTrigger { id: optionsTrigger; text: "Options"; onTriggered: optionsMenu.popupBelow(optionsTrigger) }
                    Ui.FMenuTrigger { id: helpTrigger; text: "Help"; onTriggered: helpMenu.popupBelow(helpTrigger) }
                    Item { Layout.fillWidth: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 68
                color: panelBg
                border.color: borderSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 12

                    Ui.FButton { label: "Add"; iconName: "add"; tooltipText: "Add files to the open archive or create a new one"; h: 52; enabled: !backend.busy && (backend.archivePath === "" || backend.editable); onTriggered: backend.archivePath !== "" ? addFilesDialog.open() : createDialog.open() }
                    Ui.FButton { label: "Extract To"; iconName: "download"; tooltipText: "Extract archive contents into a destination folder"; h: 52; enabled: backend.archivePath !== "" && !backend.loading; onTriggered: openExtractDialog(false) }
                    Ui.FButton { label: "Test"; iconName: "check"; tooltipText: "Verify all archive entries without extracting them"; h: 52; enabled: backend.archivePath !== "" && !backend.busy; onTriggered: backend.testArchive(passwordField.text) }
                    Ui.FButton { label: "View"; iconName: "preview"; tooltipText: "Preview the selected archive file"; h: 52; enabled: backend.selectedCount === 1 && !backend.busy; onTriggered: backend.previewSelected(passwordField.text) }
                    Ui.FButton { label: "Delete"; iconName: "delete"; tooltipText: "Remove selected entries from this archive"; h: 52; enabled: backend.editable && backend.selectedCount > 0 && !backend.busy; onTriggered: removeSelection() }
                    Ui.FButton { label: "Find"; iconName: "search"; tooltipText: "Focus the file and MIME filter"; h: 52; onTriggered: filterField.forceActiveFocus() }
                    Ui.FButton { label: "Wizard"; iconName: "zip"; tooltipText: "Create an archive with compression options"; h: 52; onTriggered: createDialog.open() }
                    Ui.FButton { label: "Info"; iconName: "info"; tooltipText: "Archive metadata and properties"; h: 52; enabled: backend.archivePath !== ""; onTriggered: { archiveProperties.text = backend.properties; propertiesDialog.open() } }
                    Item { Layout.fillWidth: true }
                    Rectangle {
                        Layout.preferredWidth: 260
                        Layout.fillHeight: true
                        radius: 10
                        color: panelBg2
                        border.color: win.frameColor
                        Column {
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 4
                            Text { text: backend.archiveName; color: textColor; font.pixelSize: 14; font.weight: Font.DemiBold; elide: Text.ElideMiddle; width: parent.width }
                            Text { text: backend.archivePath !== "" ? backend.archivePath : "No archive open"; color: mutedText; font.pixelSize: 11; elide: Text.ElideMiddle; width: parent.width }
                            Text { text: backend.count.toLocaleString() + " entries"; color: accent; font.pixelSize: 11 }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                visible: backend.tabs.length > 0
                color: chromeBg
                ListView {
                    anchors.fill: parent
                    anchors.margins: 3
                    orientation: ListView.Horizontal
                    spacing: 4
                    clip: true
                    model: backend.tabs
                    delegate: Row {
                        id: tabRow
                        required property int index
                        required property string modelData
                        spacing: 2
                        Ui.FButton { label: tabRow.modelData; primary: backend.activeTab === tabRow.index; enabled: !backend.busy; onTriggered: backend.selectTab(tabRow.index) }
                        Ui.FButton { label: "×"; enabled: !backend.busy; onTriggered: backend.closeTab(tabRow.index) }
                    }
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: panelBg2
                border.color: borderSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8
                    Label { text: "Address"; color: mutedText }
                    Ui.FButton { label: "Root"; iconName: "home"; tooltipText: "Return to the root of the archive"; h: 28; enabled: backend.currentFolder.length > 0; onTriggered: backend.rootFolder() }
                    Ui.FButton { label: "Up"; iconName: "arrow-upward"; tooltipText: "Navigate to the parent folder"; h: 28; enabled: backend.currentFolder.length > 0; onTriggered: backend.upFolder() }
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: contentBg
                        radius: 8
                        border.color: win.frameColor
                        Text {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            anchors.rightMargin: 10
                            verticalAlignment: Text.AlignVCenter
                            text: backend.archivePath !== "" ? backend.archivePath + (backend.currentFolder !== "" ? " / " + backend.currentFolder : "") : "No archive open"
                            color: textColor
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 50
                color: panelBg
                border.color: borderSoft
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    spacing: 8
                    Label { text: "Filter"; color: mutedText }
                    Ui.FTextField {
                        id: filterField
                        Layout.preferredWidth: 220
                        placeholderText: "Find files, folders or MIME types"
                        onTextChanged: backend.setFilter(text)
                    }
                    Label { text: "Password"; color: mutedText }
                    Ui.FTextField {
                        id: passwordField
                        Layout.preferredWidth: 150
                        echoMode: showPassword ? TextInput.Normal : TextInput.Password
                        placeholderText: "Archive password"
                    }
                    Ui.FCheckBox { text: "Show"; checked: showPassword; onToggled: showPassword = checked }
                    Label { text: "Conflicts"; color: mutedText }
                    Ui.FComboBox { id: existingPolicy; Layout.preferredWidth: 110; model: ["Ask", "Skip", "Rename", "Overwrite"]; currentIndex: 0; onCurrentIndexChanged: extractionConflicts = currentIndex }
                    Ui.FCheckBox { text: "Tree"; checked: treeView; onToggled: { treeView = checked; backend.setTree(checked) } }
                    Ui.FComboBox { Layout.preferredWidth: 105; model: ["Name", "Size", "Type", "Modified"]; onActivated: backend.setSort(currentIndex, descending.checked); id: sortField }
                    Ui.FCheckBox { id: descending; text: "Desc"; onToggled: backend.setSort(sortField.currentIndex, checked) }
                    Ui.FButton { label: "Select all"; h: 30; enabled: backend.count > 0; onTriggered: backend.selectAll() }
                    Ui.FButton { label: "Clear"; h: 30; enabled: backend.selectedCount > 0; onTriggered: backend.clearSelection() }
                    Item { Layout.fillWidth: true }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: contentBg
                border.color: win.frameColor
                border.width: 1
                ColumnLayout {
                    anchors.fill: parent
                    spacing: 0
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 30
                        color: panelBg2
                        border.color: borderSoft
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 14
                            Label { text: "Name"; color: textColor; font.weight: Font.DemiBold; Layout.fillWidth: true }
                            Label { text: "Size"; color: textColor; font.weight: Font.DemiBold; Layout.preferredWidth: 90 }
                            Label { text: "Type"; color: textColor; font.weight: Font.DemiBold; Layout.preferredWidth: 120 }
                            Label { text: "Modified"; color: textColor; font.weight: Font.DemiBold; Layout.preferredWidth: 140 }
                        }
                    }
                    Rectangle { Layout.fillWidth: true; height: 1; color: borderSoft }
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        ListView {
                            id: archiveList
                            anchors.fill: parent
                            model: backend.rows
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: Ui.FScrollBar { policy: ScrollBar.AsNeeded }
                            delegate: Rectangle {
                                id: entryRow
                                width: archiveList.width
                                height: 30
                                color: model.selected ? Theme.AppTheme.selected : hover.containsMouse ? Theme.AppTheme.hover : (index % 2 ? Theme.AppTheme.surface2 : Theme.AppTheme.surface3)
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 14
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8
                                        Ui.FIcon {
                                            name: model.kind === "Folder" ? "folder" : model.kind === "Link" ? "launch" :
                                                  model.mime.indexOf("zip") >= 0 || model.mime.indexOf("archive") >= 0 ? "zip" : "description"
                                            iconSize: 16
                                        }
                                        Text { text: treeView ? "  ".repeat(Math.min(20, model.path.split("/").length - 1)) + model.name : model.name; color: textColor; elide: Text.ElideMiddle; Layout.fillWidth: true }
                                    }
                                    Text { text: model.sizeText; color: mutedText; Layout.preferredWidth: 90; elide: Text.ElideRight }
                                    Text { text: model.kind; color: mutedText; Layout.preferredWidth: 120; elide: Text.ElideRight }
                                    Text { text: model.modified; color: mutedText; Layout.preferredWidth: 140; elide: Text.ElideRight }
                                }
                                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.AppTheme.borderSoft }
                                Ui.FToolTip {
                                    text: model.path + "  ·  " + model.mime + "\nCompressed: " + model.compressed + "  |  Method: " + model.method + "  |  Permissions: " + model.permissions
                                    visible: hover.containsMouse && !hover.pressed
                                }
                                MouseArea {
                                    id: hover
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                                    property real initialX: 0
                                    property real initialY: 0
                                    property bool dragged: false
                                    onPressed: function(mouse) { initialX = mouse.x; initialY = mouse.y; dragged = false }
                                    onPositionChanged: function(mouse) {
                                        if (!pressed || dragged || !(mouse.buttons & Qt.LeftButton)) return
                                        const dx = mouse.x - initialX
                                        const dy = mouse.y - initialY
                                        const distance = Qt.styleHints.startDragDistance
                                        if (dx * dx + dy * dy >= distance * distance) {
                                            dragged = true
                                            backend.dragEntry(index, passwordField.text)
                                        }
                                    }
                                    onClicked: function(mouse) { if (!dragged) { if (mouse.button === Qt.RightButton) { if (!model.selected) { backend.clearSelection(); backend.toggleVisible(index) } commandsMenu.popupBelow(entryRow) } else backend.toggleVisible(index) } }
                                    onDoubleClicked: {
                                        if (model.kind === "Folder") { backend.enterVisibleFolder(index); treeView = false; backend.setTree(false) }
                                        else { backend.clearSelection(); backend.toggleVisible(index); backend.openSelected(passwordField.text); passwordField.text = "" }
                                    }
                                    onPressAndHold: { dragged = true; backend.dragEntry(index, passwordField.text) }
                                }
                            }
                        }
                        Column {
                            anchors.centerIn: parent
                            spacing: 10
                            visible: backend.visibleCount === 0 && !backend.busy
                            Ui.FIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "zip"; iconSize: 38; iconOpacity: 0.78 }
                            Text { text: backend.count === 0 ? "No archive open" : "No items in this view"; anchors.horizontalCenter: parent.horizontalCenter; font.pixelSize: 16; font.weight: Font.DemiBold; color: textColor }
                            Text { text: backend.count === 0 ? "Open an archive or drag files here to create one." : "Try clearing the filter or moving to another folder."; anchors.horizontalCenter: parent.horizontalCenter; color: mutedText }
                            Ui.FButton { anchors.horizontalCenter: parent.horizontalCenter; label: backend.count === 0 ? "Open archive..." : "Clear filter"; primary: true; onTriggered: { if (backend.count === 0) openDialog.open(); else filterField.text = "" } }
                        }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 72
                color: chromeBg2
                border.color: borderSoft
                ColumnLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 12
                    anchors.rightMargin: 12
                    anchors.topMargin: 8
                    anchors.bottomMargin: 8
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: backend.status; color: textColor; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: backend.count.toLocaleString() + " entries"; color: mutedText }
                        Label { text: backend.selectedCount.toLocaleString() + " selected"; color: mutedText }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Label { text: backend.prepared ? backend.preparedText : (backend.busy ? backend.operation + " — " + backend.progress + (backend.queuedCount ? " | queued: " + backend.queuedCount : "") : "Ready"); color: mutedText; Layout.fillWidth: true; elide: Text.ElideRight }
                        Ui.FBusyIndicator { running: backend.busy; visible: backend.busy; implicitWidth: 24; implicitHeight: 24 }
                        Ui.FButton { label: "Copy metadata"; h: 30; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.copyEntryInfo() }
                        Ui.FButton { label: "Copy virtual"; h: 30; visible: Qt.platform.os === "windows"; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.copyVirtualSelection(passwordField.text) }
                        Ui.FButton { label: "Copy files"; iconName: "content-copy"; h: 30; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.copySelection(passwordField.text) }
                        Ui.FButton { label: "Prepare URLs"; h: 30; enabled: backend.selectedCount > 0 && !backend.busy; onTriggered: backend.prepareDrag(passwordField.text) }
                        Ui.FButton { label: "Open staged"; h: 30; enabled: backend.prepared; onTriggered: backend.showExportFolder() }
                        Ui.FButton { label: "Cancel"; h: 30; enabled: backend.busy; onTriggered: backend.cancel() }
                    }
                }
            }
        }
    }

    Rectangle {
        id: toast
        visible: alertText.length > 0
        z: 4
        width: Math.min(520, win.width - 40)
        height: 46
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 20
        anchors.bottomMargin: 20
        color: panelBg2
        border.color: accent
        radius: 8
        RowLayout {
            anchors.fill: parent
            anchors.margins: 8
            Text { text: alertText; color: textColor; Layout.fillWidth: true; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
            Ui.FButton { label: "OK"; h: 28; onTriggered: alertText = "" }
        }
    }
    // Findex-style native edge resizing for the frameless window.
    Item {
        id: resizeEdges
        anchors.fill: parent
        z: 9000
        visible: win.visibility !== Window.Maximized
        readonly property int edge: 5
        readonly property int corner: 12
        MouseArea {
            anchors { left: parent.left; right: parent.right; top: parent.top }
            height: resizeEdges.edge
            cursorShape: Qt.SizeVerCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.TopEdge) }
        }
        MouseArea {
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
            height: resizeEdges.edge
            cursorShape: Qt.SizeVerCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.BottomEdge) }
        }
        MouseArea {
            anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
            width: resizeEdges.edge
            cursorShape: Qt.SizeHorCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.LeftEdge) }
        }
        MouseArea {
            anchors { right: parent.right; top: parent.top; bottom: parent.bottom }
            width: resizeEdges.edge
            cursorShape: Qt.SizeHorCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.RightEdge) }
        }
        MouseArea {
            anchors { left: parent.left; top: parent.top }
            width: resizeEdges.corner; height: resizeEdges.corner
            cursorShape: Qt.SizeFDiagCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.TopEdge | Qt.LeftEdge) }
        }
        MouseArea {
            anchors { right: parent.right; top: parent.top }
            width: resizeEdges.corner; height: resizeEdges.corner
            cursorShape: Qt.SizeBDiagCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.TopEdge | Qt.RightEdge) }
        }
        MouseArea {
            anchors { left: parent.left; bottom: parent.bottom }
            width: resizeEdges.corner; height: resizeEdges.corner
            cursorShape: Qt.SizeBDiagCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.BottomEdge | Qt.LeftEdge) }
        }
        MouseArea {
            anchors { right: parent.right; bottom: parent.bottom }
            width: resizeEdges.corner; height: resizeEdges.corner
            cursorShape: Qt.SizeFDiagCursor
            onPressed: function(mouse) { mouse.accepted = win.startSystemResize(Qt.BottomEdge | Qt.RightEdge) }
        }
    }

}
