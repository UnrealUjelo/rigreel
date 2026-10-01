import QtQuick
import QtQuick.Controls.Basic
import Director

// Checkbox + label.
Item {
    id: root
    property string text: ""
    property string tooltip: ""
    property bool checked: false
    signal toggled(bool on)
    implicitWidth: box.width + (text ? label.implicitWidth + 8 : 0)
    implicitHeight: 22
    function click() { toggled(!checked) }
    Rectangle {
        id: box
        width: 14; height: 14; radius: 3
        anchors.verticalCenter: parent.verticalCenter
        color: root.checked ? Theme.accent : ma.containsMouse ? Theme.numHover : Theme.num
        border.color: "transparent"
        Icon { anchors.centerIn: parent; name: "check"; size: 11; color: "white"; visible: root.checked }
    }
    Text {
        id: label
        x: box.width + 8
        anchors.verticalCenter: parent.verticalCenter
        text: root.text
        color: Theme.text
        font.pixelSize: Theme.fontSize
        width: Math.min(implicitWidth, root.width - x)
        elide: Text.ElideRight
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.click() }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}
