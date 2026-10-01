import QtQuick
import QtQuick.Controls.Basic
import Director

// Text button. kind: "" (normal) | "primary" | "danger" | "ghost"
Item {
    id: root
    property string text: ""
    property string icon: ""
    property string kind: ""
    property string tooltip: ""
    property bool checked: false
    property bool small: true
    signal clicked()
    implicitWidth: row.implicitWidth + (small ? 16 : 22)
    implicitHeight: small ? 24 : 28
    opacity: enabled ? 1 : 0.45

    function click() { if (enabled) clicked() }

    readonly property color base: kind === "primary" ? Theme.accent : kind === "danger" ? "#7a3030" : kind === "ghost" ? "transparent" : Theme.bg3
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.checked ? Theme.accent : ma.pressed ? Qt.darker(root.base === "transparent" ? Theme.bg3 : root.base, 1.2)
                                           : ma.containsMouse ? Qt.lighter(root.base === "transparent" ? Theme.bg3 : root.base, 1.15) : root.base
        border.color: "transparent"
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Icon { name: root.icon; visible: root.icon !== ""; size: 13; color: root.kind === "danger" ? "#ffb0b0" : Theme.textBright; anchors.verticalCenter: parent.verticalCenter }
        Text {
            text: root.text
            color: root.kind === "danger" ? "#ffd0d0" : Theme.textBright
            font.pixelSize: Theme.fontSize
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.click()
    }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}
