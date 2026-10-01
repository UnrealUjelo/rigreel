import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// Game ▸ Games…: the games you own that Director can make films with. Director reads the installed game's own
// files (nothing is copied or shared) through a game plugin; open one and its characters, props, sets and
// animations appear in the Asset Browser.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg

    readonly property var lib: studio.games
    readonly property var installed: lib.games
    readonly property var plugins: lib.plugins
    // supported games that are not installed (or not found): shown greyed so users know what else works
    readonly property var missing: {
        const have = {}
        for (const g of installed) have[g.plugin + ":" + g.id] = true
        const out = []
        for (const p of plugins)
            for (const g of p.games)
                if (!have[p.id + ":" + g.id]) out.push({ title: g.title, support: g.support, plugin: p.name })
        return out
    }

    function supportText(s) {
        return s === "full" ? "Supported" : s === "partial" ? "Partly supported" : s === "untested" ? "Untested" : "Not supported yet"
    }
    function supportColor(s) {
        return s === "full" ? Theme.ok : s === "partial" ? Theme.amber : s === "untested" ? Theme.muted : Theme.danger
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            spacing: 8
            Icon { name: "gamepad-2"; size: 20; color: Theme.amber }
            Text { text: "Games"; color: Theme.textBright; font.pixelSize: 16; font.bold: true }
            Item { Layout.fillWidth: true }
            Btn { text: "Rescan"; icon: "refresh-cw"; onClicked: root.lib.refresh() }
            Btn { text: "Add a Game Folder…"; icon: "folder-open"; onClicked: studio.addGameFolder() }
        }
        Text {
            Layout.fillWidth: true
            wrapMode: Text.WordWrap
            color: Theme.muted
            font.pixelSize: Theme.smallSize
            text: "Director makes films with the games you own. It reads the installed game's files directly (the game does not need to run) and nothing leaves your PC. Open a game and its characters, props, sets and animations appear in the Asset Browser."
        }

        // installed games
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: Theme.field
            border.color: Theme.border
            radius: Theme.radius
            ListView {
                id: list
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                model: root.installed
                ScrollBar.vertical: ScrollBar {}
                delegate: Rectangle {
                    id: row
                    required property var modelData
                    readonly property bool active: modelData.key === root.lib.activeKey
                    width: list.width
                    height: 58
                    color: active ? Qt.rgba(0.24, 0.48, 0.77, 0.18) : hover.hovered ? Theme.bg3 : "transparent"
                    HoverHandler { id: hover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 10
                        spacing: 10
                        Icon { name: row.active ? "circle-check" : "gamepad-2"; size: 18; color: row.active ? Theme.ok : Theme.muted }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            RowLayout {
                                spacing: 8
                                Text { text: row.modelData.title; color: Theme.textBright; font.pixelSize: 13; font.bold: row.active }
                                Text { text: root.supportText(row.modelData.support); color: root.supportColor(row.modelData.support); font.pixelSize: Theme.smallSize }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: row.modelData.folder
                                color: Theme.faint
                                font.pixelSize: Theme.smallSize
                                elide: Text.ElideMiddle
                            }
                            Text {
                                visible: !!row.modelData.note
                                text: row.modelData.note || ""
                                color: Theme.muted
                                font.pixelSize: Theme.smallSize
                            }
                        }
                        Text {
                            visible: row.active
                            text: root.lib.opening ? root.lib.status : root.lib.ready ? "Open" : ""
                            color: root.lib.opening ? Theme.amber : Theme.ok
                            font.pixelSize: Theme.smallSize
                        }
                        Btn {
                            visible: !row.active || (!root.lib.ready && !root.lib.opening)
                            text: "Open"
                            kind: "primary"
                            enabled: !root.lib.opening
                            onClicked: {
                                studio.runtimeMode = "standalone"
                                root.lib.activate(row.modelData.key)
                            }
                        }
                    }
                    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                }
                Text {
                    anchors.centerIn: parent
                    width: parent.width - 40
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    visible: list.count === 0
                    color: Theme.muted
                    text: root.plugins.length === 0
                          ? "No game plugins were loaded. They live in plugins/games next to RigReelStudio.exe."
                          : "No supported game was found in your Steam libraries. If one is installed somewhere else, use Add a Game Folder."
                }
            }
        }

        // what else the plugins can read
        Text {
            visible: root.missing.length > 0
            text: "Also supported by your plugins (not found on this PC)"
            color: Theme.muted
            font.pixelSize: Theme.smallSize
            font.bold: true
        }
        Flow {
            Layout.fillWidth: true
            visible: root.missing.length > 0
            spacing: 6
            Repeater {
                model: root.missing
                Rectangle {
                    required property var modelData
                    width: lbl.implicitWidth + 16
                    height: 22
                    radius: 11
                    color: Theme.bg2
                    border.color: Theme.border
                    Text {
                        id: lbl
                        anchors.centerIn: parent
                        text: parent.modelData.title + (parent.modelData.support === "full" ? "" : " · " + root.supportText(parent.modelData.support).toLowerCase())
                        color: Theme.muted
                        font.pixelSize: Theme.smallSize
                    }
                }
            }
        }

        RowLayout {
            Text {
                Layout.fillWidth: true
                text: root.plugins.map(p => p.name + " " + p.version).join(" · ")
                color: Theme.faint
                font.pixelSize: Theme.smallSize
                elide: Text.ElideRight
            }
            Btn { text: "Close"; onClicked: root.closeRequested() }
        }
    }
}
