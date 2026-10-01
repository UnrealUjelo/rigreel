import QtQuick
import QtQuick.Layouts
import Director

// "Label   [control]" row with a fixed label column so forms line up.
RowLayout {
    id: root
    property string label: ""
    property int labelWidth: 96
    property string tooltip: ""
    default property alias content: holder.data
    Layout.fillWidth: true
    spacing: 8
    Text {
        text: root.label
        color: Theme.muted
        font.pixelSize: Theme.smallSize
        Layout.preferredWidth: root.labelWidth
        Layout.alignment: Qt.AlignVCenter
        horizontalAlignment: Text.AlignRight
        elide: Text.ElideRight
    }
    RowLayout {
        id: holder
        Layout.fillWidth: true
        spacing: 6
    }
}
