import QtQuick
import QtQuick.Controls.Basic
import Director

// Dropdown: shows the current option; the list opens as a native menu (never clipped, floats over the game).
// options: [{ v, label }]
Item {
    id: root
    property var options: []
    property var value
    property string placeholder: "Choose…"
    property string tooltip: ""
    property string icon: ""
    signal chosen(var v)
    implicitWidth: 140
    implicitHeight: 22

    readonly property string currentLabel: {
        for (let i = 0; i < options.length; ++i) if (options[i].v === value || String(options[i].v) === String(value)) return options[i].label
        return placeholder
    }
    function click() {
        const items = options.map(o => ({ text: o.label, value: o.v, checked: o.v === value || String(o.v) === String(value) }))
        const r = studio.popup(items)
        if (r !== null && r !== undefined) root.chosen(r)
    }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: ma.containsMouse ? "#2f2f2f" : "#282828"
        border.color: ma.containsMouse ? "#4a4a4a" : "#3d3d3d"
    }
    Row {
        anchors.left: parent.left; anchors.leftMargin: 7
        anchors.right: chev.left; anchors.rightMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        Icon { name: root.icon; visible: root.icon !== ""; size: 13; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
        Text {
            width: parent.width - (root.icon ? 18 : 0)
            text: root.currentLabel
            color: Theme.text
            font.pixelSize: Theme.smallSize
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    Icon { id: chev; name: "chevron-down"; size: 12; color: Theme.muted; anchors.right: parent.right; anchors.rightMargin: 5; anchors.verticalCenter: parent.verticalCenter }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.click() }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}
