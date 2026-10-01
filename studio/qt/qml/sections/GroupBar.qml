import QtQuick
import QtQuick.Layouts
import Director

// Several characters selected (Shift+click): act on all of them at once.
Rectangle {
    id: bar
    Store { id: s }
    readonly property int n: s.sel && s.sel.multi ? s.sel.multi.length : 0
    visible: n > 1
    width: parent ? parent.width : 300
    height: visible ? flow.implicitHeight + 12 : 0
    color: "#2a3140"
    Flow {
        id: flow
        x: 8; y: 6
        width: parent.width - 16
        spacing: 4
        Chip { text: bar.n + " selected"; checked: true }
        Btn { kind: "primary"; icon: "key"; text: "Key all @ " + s.t; tooltip: "Key the position of every selected character at the playhead"; onClicked: s.cmd("group_key", { t: s.t }) }
        Btn { text: "Release all"; onClicked: s.send("group_release") }
        Btn { kind: "danger"; text: "Remove all"; tooltip: "Remove every selected spawned character / prop"; onClicked: s.cmd("group_remove") }
        Btn { kind: "ghost"; text: "Clear"; onClicked: s.send("select_clear") }
    }
}
