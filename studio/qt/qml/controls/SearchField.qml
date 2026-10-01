import QtQuick
import QtQuick.Controls.Basic
import Director

// Search / filter box with a clear button. `text` updates as you type.
TextField {
    id: root
    property string icon: "search"
    implicitHeight: 26
    leftPadding: 28
    rightPadding: 24
    color: Theme.textBright
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accent
    selectByMouse: true
    font.pixelSize: Theme.fontSize
    background: Rectangle {
        radius: Theme.radius
        color: Theme.field
        border.color: root.activeFocus ? Theme.accent : Theme.border
    }
    Icon { name: root.icon; size: 14; color: Theme.muted; x: 8; anchors.verticalCenter: parent.verticalCenter }
    IconButton {
        visible: root.text !== ""
        icon: "x"; size: 20; iconSize: 12
        anchors.right: parent.right; anchors.rightMargin: 3
        anchors.verticalCenter: parent.verticalCenter
        onClicked: root.text = ""
    }
    Keys.onEscapePressed: { if (text) text = ""; else focus = false }
}
