import QtQuick
import QtQuick.Controls.Basic
import Director

// The key diamond beside an animatable value (SFM's channel key). Hollow: not animated; grey: animated;
// amber: a key at this frame. Click keys the value here (again: removes that key); right-click stops animating it.
Item {
    id: root
    property string channel: ""             // "light:3/intensity"
    readonly property var info: studio.state && studio.state.channels ? studio.state.channels[channel] : undefined
    readonly property bool animated: !!info
    readonly property bool here: !!info && info.here
    implicitWidth: 18
    implicitHeight: 20
    visible: channel !== "" && studio.standalone

    Rectangle {
        anchors.centerIn: parent
        width: 9; height: 9
        rotation: 45
        radius: 1
        color: root.here ? Theme.amber : root.animated ? "#8e8e98" : "transparent"
        border.color: root.here ? Theme.amber : ma.containsMouse ? Theme.textBright : root.animated ? "#8e8e98" : Theme.faint
        border.width: 1.5
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: (m) => {
            if (m.button === Qt.RightButton) {
                if (!root.animated) return
                const r = studio.popup([...(root.here ? [{ text: "Remove the key here", value: "key" }] : []), { text: "Stop animating (keep this value)", value: "clear" }])
                if (r === "key") studio.cmd("remove_channel_key", { channel: root.channel })
                else if (r === "clear") studio.cmd("channel_clear", { channel: root.channel })
            } else if (root.here) studio.cmd("remove_channel_key", { channel: root.channel })
            else studio.cmd("key_channel", { channel: root.channel })
        }
    }
    ToolTip.visible: ma.containsMouse
    ToolTip.delay: 500
    ToolTip.text: root.here ? "Keyed at this frame: click to remove the key, right-click for more"
                            : root.animated ? "Animated (" + root.info.keys + " keys): click to key it here; changing the value keys it too"
                                            : "Animate: key this value at the current frame"
}
