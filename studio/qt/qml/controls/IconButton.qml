import QtQuick
import QtQuick.Controls.Basic
import Director

// Square tool button: an icon, an optional label, a tooltip. Never takes keyboard focus, so Space and the
// single-letter shortcuts keep going to the timeline.
Item {
    id: root
    property string icon: ""
    property string text: ""
    property string tooltip: ""
    property bool checked: false
    property bool accent: false          // checked state in amber (live / recording) instead of blue
    property bool danger: false
    property bool flat: true
    property int size: 26
    property int iconSize: 15
    property color iconColor: !enabled ? Theme.faint : checked ? Theme.textBright : danger ? Theme.danger : ma.containsMouse ? Theme.textBright : Theme.text
    signal clicked()
    implicitWidth: text ? row.implicitWidth + 14 : size
    implicitHeight: size
    opacity: enabled ? 1 : 0.55

    function click() { if (enabled) clicked() }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.checked ? (root.accent ? "#7a5a1c" : Theme.accent) : ma.pressed ? Theme.numHover : ma.containsMouse ? "#454545" : (root.flat ? "transparent" : Theme.bg3)
        border.color: root.checked && root.accent ? Theme.amber : "transparent"
        border.width: 1
    }
    Row {
        id: row
        anchors.centerIn: parent
        spacing: 5
        Icon { name: root.icon; color: root.iconColor; size: root.iconSize; visible: root.icon !== ""; anchors.verticalCenter: parent.verticalCenter }
        Text {
            text: root.text; visible: root.text !== ""
            color: root.iconColor; font.pixelSize: Theme.fontSize
            anchors.verticalCenter: parent.verticalCenter
        }
    }
    MouseArea {
        id: ma
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton
        cursorShape: root.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: root.click()
    }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}
