import QtQuick
import QtQuick.Controls.Basic
import Director

// Plain text input. `committed(text)` on Enter or when it loses focus with a changed value.
TextField {
    id: root
    signal committed(string text)
    property string original: ""
    implicitHeight: 24
    leftPadding: 7
    rightPadding: 7
    color: Theme.textBright
    placeholderTextColor: Theme.faint
    selectionColor: Theme.accent
    selectByMouse: true
    font.pixelSize: Theme.fontSize
    background: Rectangle {
        radius: Theme.radius
        color: Theme.field
        border.color: root.activeFocus ? Theme.accent : "transparent"
    }
    onActiveFocusChanged: if (activeFocus) original = text; else if (text !== original) committed(text)
    onAccepted: { original = text; committed(text); focus = false }
    Keys.onEscapePressed: { text = original; focus = false }
}
