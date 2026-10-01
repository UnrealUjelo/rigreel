import QtQuick
import QtQuick.Layouts
import Director

// A collapsible panel, like Blender's properties panels: a rounded box with a header (caret, icon, title).
Column {
    id: root
    property string title: ""
    property string icon: ""
    property string note: ""             // small grey text right of the title
    property bool open: true
    default property alias content: body.data
    width: parent ? parent.width : 300
    spacing: 0
    topPadding: 3
    bottomPadding: 3

    Rectangle {
        x: 5
        width: root.width - 10
        height: head.height + (root.open ? body.implicitHeight + 12 : 0)
        radius: Theme.radius
        color: Theme.panel
        Rectangle {
            id: head
            width: parent.width
            height: 26
            radius: Theme.radius
            color: ma.containsMouse ? "#3a3a3a" : "transparent"
            Row {
                anchors.left: parent.left; anchors.leftMargin: 7
                anchors.verticalCenter: parent.verticalCenter
                spacing: 6
                Icon { name: root.open ? "chevron-down" : "chevron-right"; size: 12; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                Icon { name: root.icon; visible: root.icon !== ""; size: 14; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
                Text { text: root.title; color: Theme.text; font.pixelSize: Theme.fontSize; anchors.verticalCenter: parent.verticalCenter }
                Text { text: root.note; visible: root.note !== ""; color: Theme.muted; font.pixelSize: Theme.smallSize; anchors.verticalCenter: parent.verticalCenter }
            }
            MouseArea { id: ma; anchors.fill: parent; hoverEnabled: true; onClicked: root.open = !root.open }
        }
        ColumnLayout {
            id: body
            visible: root.open
            x: 10; y: head.height + 2
            width: parent.width - 20
            spacing: 5
        }
    }
}
