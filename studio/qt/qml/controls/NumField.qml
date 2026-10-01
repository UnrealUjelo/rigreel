import QtQuick
import QtQuick.Controls.Basic
import Director

// Number box you can scrub: drag left / right to change it (Shift: fine, Ctrl: coarse), click to type.
// `edited(v)` fires while dragging (at most ~25 times a second) and once more when you let go / press Enter.
Item {
    id: root
    property real value: 0
    property real step: 0.01
    property int decimals: step >= 1 ? 0 : step >= 0.1 ? 1 : step >= 0.01 ? 2 : 3
    property real from: -1e9
    property real to: 1e9
    property string axis: ""             // "X" / "Y" / "Z" tag on the left
    property color axisColor: Theme.muted
    property string suffix: ""
    property string tooltip: ""
    signal edited(real v)
    implicitWidth: 70
    implicitHeight: 22

    property real shown: dragging || editing ? local : value
    property real local: 0
    property bool dragging: false
    property bool editing: false
    property real pending: NaN

    function clamp(v) { return Math.max(from, Math.min(to, v)) }
    function fmt(v) { return (isFinite(v) ? v.toFixed(decimals) : "-") + suffix }
    function push(v) { pending = v; if (!throttle.running) { root.edited(v); pending = NaN; throttle.start() } }

    Timer { id: throttle; interval: 40; onTriggered: if (!isNaN(root.pending)) { root.edited(root.pending); root.pending = NaN } }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.editing ? Theme.field : ma.containsMouse || root.dragging ? Theme.numHover : Theme.num
        border.color: root.editing ? Theme.accent : "transparent"
    }
    Rectangle {
        visible: root.axis !== ""
        width: 3; height: parent.height - 6; x: 3; y: 3; radius: 1
        color: root.axisColor
    }
    Text {
        visible: !root.editing
        anchors.fill: parent
        anchors.leftMargin: root.axis ? 10 : 6
        anchors.rightMargin: 6
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        text: root.fmt(root.shown)
        color: Theme.textBright
        font.pixelSize: Theme.smallSize
        font.family: Theme.mono
        elide: Text.ElideRight
    }
    TextInput {
        id: input
        visible: root.editing
        anchors.fill: parent
        anchors.leftMargin: 6; anchors.rightMargin: 6
        verticalAlignment: TextInput.AlignVCenter
        horizontalAlignment: TextInput.AlignHCenter
        color: Theme.textBright
        selectionColor: Theme.accent
        font.pixelSize: Theme.smallSize
        font.family: Theme.mono
        selectByMouse: true
        onAccepted: { const v = parseFloat(text); if (isFinite(v)) root.edited(root.clamp(v)); root.editing = false }
        onActiveFocusChanged: if (!activeFocus && root.editing) accepted()
        Keys.onEscapePressed: root.editing = false
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        enabled: !root.editing
        hoverEnabled: true
        cursorShape: Qt.SizeHorCursor
        property real x0: 0
        property real v0: 0
        onPressed: (m) => { x0 = m.x; v0 = root.value; root.local = root.value }
        onPositionChanged: (m) => {
            if (!pressed) return
            const dx = m.x - x0
            if (!root.dragging && Math.abs(dx) < 3) return
            root.dragging = true
            const mul = (m.modifiers & Qt.ShiftModifier) ? 0.1 : (m.modifiers & Qt.ControlModifier) ? 10 : 1
            root.local = root.clamp(v0 + Math.round(dx / 2) * root.step * mul)
            root.push(root.local)
        }
        onReleased: {
            if (root.dragging) { root.dragging = false; throttle.stop(); root.edited(root.local); root.pending = NaN; return }
            root.editing = true
            input.text = root.value.toFixed(root.decimals)
            input.forceActiveFocus()
            input.selectAll()
        }
    }
    ToolTip.visible: tooltip !== "" && ma.containsMouse && !dragging
    ToolTip.text: tooltip
    ToolTip.delay: 600
}
