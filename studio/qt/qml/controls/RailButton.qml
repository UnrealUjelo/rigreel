import QtQuick
import Director

// A category button in the narrow rail on the left of a browser.
Rectangle {
    id: root
    property string text: ""
    property bool checked: false
    signal clicked()
    function click() { clicked() }
    width: parent ? parent.width : 80
    height: 26
    color: checked ? Theme.rowSel : ma.containsMouse ? Theme.rowHover : "transparent"
    Text {
        anchors.verticalCenter: parent.verticalCenter
        x: 10; width: parent.width - 14
        text: root.text
        color: root.checked ? Theme.textBright : Theme.text
        font.pixelSize: Theme.smallSize
        elide: Text.ElideRight
    }
    MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; cursorShape: Qt.PointingHandCursor; onClicked: root.click() }
}
