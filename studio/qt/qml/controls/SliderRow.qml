import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// Label + slider + value, SFM style: the bar fills to the value; drag anywhere on it, double-click to type.
// Shift while dragging = fine. `edited(v)` fires live (throttled) and on release.
Field {
    id: root
    property real value: 0
    property real from: 0
    property real to: 1
    property real step: 0.01
    property var format: null           // function(v) -> string
    property color fill: Theme.accent
    property string channel: ""         // "light:3/intensity": a key diamond to animate the value (standalone)
    signal edited(real v)

    property real shown: bar.dragging ? bar.local : value
    function fmt(v) { return format ? format(v) : v.toFixed(step >= 1 ? 0 : step >= 0.1 ? 1 : 2) }

    Item {
        id: bar
        Layout.fillWidth: true
        implicitHeight: 22
        property bool dragging: false
        property real local: 0
        property real pending: NaN
        function setFromX(x, fine) {
            let t = Math.max(0, Math.min(1, x / width))
            let v = root.from + t * (root.to - root.from)
            v = Math.round(v / root.step) * root.step
            local = Math.max(root.from, Math.min(root.to, v))
            pending = local
            if (!throttle.running) { root.edited(local); pending = NaN; throttle.start() }
        }
        Timer { id: throttle; interval: 40; onTriggered: if (!isNaN(bar.pending)) { root.edited(bar.pending); bar.pending = NaN } }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radius
            color: ma.containsMouse || bar.dragging ? Theme.numHover : Theme.num
            border.color: "transparent"
            clip: true
            Rectangle {
                x: 1; y: 1
                height: parent.height - 2
                width: Math.max(0, Math.min(1, (root.shown - root.from) / Math.max(1e-9, root.to - root.from))) * (parent.width - 2)
                radius: Theme.radius - 1
                color: root.fill
            }
        }
        Text {
            visible: !edit.visible
            anchors.centerIn: parent
            text: root.fmt(root.shown)
            color: Theme.textBright
            font.pixelSize: Theme.smallSize
            font.family: Theme.mono
        }
        TextInput {
            id: edit
            visible: false
            anchors.fill: parent
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.textBright
            font.pixelSize: Theme.smallSize
            font.family: Theme.mono
            selectByMouse: true
            onAccepted: { const v = parseFloat(text); if (isFinite(v)) root.edited(v); visible = false }
            onActiveFocusChanged: if (!activeFocus && visible) visible = false
            Keys.onEscapePressed: visible = false
        }
        MouseArea {
            id: ma
            anchors.fill: parent
            enabled: !edit.visible
            hoverEnabled: true
            cursorShape: Qt.SizeHorCursor
            property real x0: 0
            property real v0: 0
            onPressed: (m) => { x0 = m.x; v0 = root.value; bar.local = root.value; bar.dragging = true; if (!(m.modifiers & Qt.ShiftModifier)) bar.setFromX(m.x) }
            onPositionChanged: (m) => {
                if (!pressed) return
                if (m.modifiers & Qt.ShiftModifier) {
                    const v = v0 + (m.x - x0) / parent.width * (root.to - root.from) * 0.1
                    bar.setFromX((v - root.from) / (root.to - root.from) * parent.width)
                } else bar.setFromX(m.x)
            }
            onReleased: { bar.dragging = false; throttle.stop(); root.edited(bar.local); bar.pending = NaN }
            onDoubleClicked: { edit.text = root.value.toFixed(3); edit.visible = true; edit.forceActiveFocus(); edit.selectAll() }
        }
    }
    KeyButton { channel: root.channel }
}
