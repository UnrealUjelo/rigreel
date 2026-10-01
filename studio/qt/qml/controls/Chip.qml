import QtQuick
import QtQuick.Controls.Basic
import Director

// Small toggle pill (filters, quick choices).
Item {
    id: root
    property string text: ""
    property string tooltip: ""
    property bool checked: false
    property color tint: Theme.accent
    signal clicked()
    implicitWidth: label.implicitWidth + 16
    implicitHeight: 22
    function click() { clicked() }
    Rectangle {
        anchors.fill: parent
        radius: Theme.radius
        color: root.checked ? root.tint : ma.containsMouse ? Theme.numHover : Theme.num
        border.color: "transparent"
    }
    Text {
        id: label
        anchors.centerIn: parent
        text: root.text
        color: root.checked ? "white" : Theme.text
        font.pixelSize: Theme.smallSize
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.click() }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 500
}
