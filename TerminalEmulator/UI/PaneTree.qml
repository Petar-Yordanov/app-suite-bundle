import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import AppSuite.Terminal 1.0

Item {
    id: tree
    property var node: null
    property var host: null
    Loader {
        anchors.fill: parent
        sourceComponent: !tree.node ? null : tree.node.leaf ? leafComponent : branchComponent
    }
    Component {
        id: leafComponent
        Rectangle {
            color: "transparent"
            border.color: tree.node && controller.active === tree.node.session ? "#618bdd" : tree.host.lineColor
            border.width: 1
            ColumnLayout {
                anchors.fill: parent; anchors.margins: 1; spacing: 0
                Rectangle {
                    Layout.fillWidth: true; Layout.preferredHeight: 29
                    color: tree.host.panelColor
                    RowLayout {
                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 5
                        Label {
                            text: tree.node && tree.node.session ? tree.node.session.title : ""
                            Layout.fillWidth: true; elide: Text.ElideRight; font.pixelSize: 12
                        }
                        Label {
                            visible: !!tree.node && !!tree.node.session && !tree.node.session.running
                            text: tree.node && tree.node.session ? tree.node.session.status : ""
                            color: tree.host.mutedColor; font.pixelSize: 11; elide: Text.ElideRight
                            Layout.maximumWidth: 220
                        }
                        ToolButton {
                            text: "Restart"; visible: !!tree.node && !!tree.node.session && !tree.node.session.running
                            onClicked: tree.node.session.restart()
                        }
                        ToolButton {
                            text: "×"
                            onClicked: {
                                controller.activate(tree.node.session)
                                controller.closePane()
                            }
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    TerminalSurface {
                        id: terminal
                        anchors.fill: parent
                        anchors.rightMargin: 13
                        session: tree.node ? tree.node.session : null
                        options: controller.options
                        onActivated: {
                            controller.activate(session)
                            tree.host.activeView = terminal
                        }
                        Component.onCompleted: Qt.callLater(function() {
                            if (session && controller.active === session) {
                                terminal.forceActiveFocus()
                                tree.host.activeView = terminal
                            }
                        })
                        Connections {
                            target: controller
                            function onActiveChanged() {
                                if (terminal.session && controller.active === terminal.session) {
                                    terminal.forceActiveFocus()
                                    tree.host.activeView = terminal
                                }
                            }
                        }
                    }
                    ScrollBar {
                        id: scroll
                        anchors.right: parent.right; anchors.top: parent.top; anchors.bottom: parent.bottom
                        width: 13; orientation: Qt.Vertical
                        visible: !!terminal.session && terminal.session.history > 0
                        size: terminal.session ? Math.min(1, terminal.session.rows / (terminal.session.rows + terminal.session.history)) : 1
                        policy: ScrollBar.AlwaysOn
                        Binding {
                            target: scroll; property: "position"
                            when: !scroll.pressed
                            value: !terminal.session || terminal.session.history === 0 ? 0 :
                                (1 - terminal.session.offset / terminal.session.history) * (1 - scroll.size)
                        }
                        onPositionChanged: {
                            if (pressed && terminal.session && size < 1)
                                terminal.session.offset = Math.round(terminal.session.history * (1 - position / (1 - size)))
                        }
                    }
                }
            }
        }
    }
    Component {
        id: branchComponent
        Item {
            id: branch
            property bool horizontal: tree.node ? tree.node.horizontal : true
            property real ratio: tree.node ? tree.node.ratio : .5
            property real gapSize: 6
            property real primaryWidth: Math.max(0, (width-gapSize)*ratio)
            property real primaryHeight: Math.max(0, (height-gapSize)*ratio)
            function syncChildren() {
                if (!tree.node || tree.node.leaf) return
                if (!first.item || first.item.node !== tree.node.first) {
                    first.source = ""
                    first.setSource("PaneTree.qml", {node:tree.node.first, host:tree.host})
                }
                if (!second.item || second.item.node !== tree.node.second) {
                    second.source = ""
                    second.setSource("PaneTree.qml", {node:tree.node.second, host:tree.host})
                }
            }
            Component.onCompleted: syncChildren()
            Connections {
                target: tree.node
                function onChanged() { branch.syncChildren() }
            }
            Connections {
                target: tree
                function onNodeChanged() { branch.syncChildren() }
            }
            Loader {
                id: first
                x: 0; y: 0
                width: branch.horizontal ? branch.primaryWidth : branch.width
                height: branch.horizontal ? branch.height : branch.primaryHeight
            }
            Rectangle {
                x: branch.horizontal ? branch.primaryWidth : 0
                y: branch.horizontal ? 0 : branch.primaryHeight
                width: branch.horizontal ? branch.gapSize : branch.width
                height: branch.horizontal ? branch.height : branch.gapSize
                color: resizeArea.containsMouse || resizeArea.pressed ? "#618bdd" : tree.host.lineColor
                MouseArea {
                    id: resizeArea
                    anchors.fill: parent
                    hoverEnabled: true
                    cursorShape: branch.horizontal ? Qt.SplitHCursor : Qt.SplitVCursor
                    onPositionChanged: function(mouse) {
                        if (!pressed || !tree.node) return
                        let p = mapToItem(branch, mouse.x, mouse.y)
                        tree.node.ratio = branch.horizontal ? p.x / Math.max(1, branch.width) : p.y / Math.max(1, branch.height)
                    }
                }
            }
            Loader {
                id: second
                x: branch.horizontal ? branch.primaryWidth + branch.gapSize : 0
                y: branch.horizontal ? 0 : branch.primaryHeight + branch.gapSize
                width: branch.horizontal ? Math.max(0, branch.width - x) : branch.width
                height: branch.horizontal ? branch.height : Math.max(0, branch.height - y)
            }
        }
    }
}
