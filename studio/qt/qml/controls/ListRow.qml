import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// A browser row: icon, title, subtitle, actions on the right. Hover and selection states; `entered` / `exited`
// drive the in-game previews (characters, props, sounds, animations).
Rectangle {
    id: root
    property string icon: ""
    property string image: ""                   // a thumbnail instead of the icon (image://thumb/...)
    property color iconColor: "#aab6c8"
    property string title: ""
    property string subtitle: ""
    property string tooltip: ""
    property bool selected: false
    property bool dim: false
    property int indent: 0
    default property alias actions: act.data
    signal clicked(var mouse)
    signal doubleClicked()
    signal entered()
    signal exited()
    signal rightClicked()
    function click() { clicked(null) }        // automation (MCP studio_click)
    implicitHeight: image ? 48 : subtitle ? 38 : 28
    width: ListView.view ? ListView.view.width : (parent ? parent.width : 200)
    color: selected ? Theme.rowSel : ma.containsMouse ? Theme.rowHover : "transparent"
    opacity: dim ? 0.5 : 1
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8 + root.indent
        anchors.rightMargin: 6
        spacing: 8
        Rectangle {
            visible: root.image !== ""
            implicitWidth: 40; implicitHeight: 40; radius: 4
            color: "#3a3a3a"
            Image {
                anchors.fill: parent
                anchors.margins: 1
                source: root.image
                sourceSize: Qt.size(96, 96)
                asynchronous: true
                cache: true
                fillMode: Image.PreserveAspectFit
                smooth: true
            }
        }
        Rectangle {
            visible: root.icon !== "" && root.image === ""
            implicitWidth: 26; implicitHeight: 26; radius: 4
            color: "#3a3a3a"
            Icon { anchors.centerIn: parent; name: root.icon; size: 15; color: root.iconColor }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 0
            Text { Layout.fillWidth: true; text: root.title; color: Theme.textBright; font.pixelSize: Theme.fontSize; elide: Text.ElideRight }
            Text { visible: root.subtitle !== ""; Layout.fillWidth: true; text: root.subtitle; color: Theme.muted; font.pixelSize: Theme.smallSize; elide: Text.ElideRight }
        }
        RowLayout { id: act; spacing: 3; visible: ma.containsMouse || root.selected || always; property bool automationReveal: true }
    }
    property bool always: false
    MouseArea {
        id: ma
        anchors.fill: parent
        z: -1
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onEntered: root.entered()
        onExited: root.exited()
        onClicked: (m) => m.button === Qt.RightButton ? root.rightClicked() : root.clicked(m)
        onDoubleClicked: root.doubleClicked()
    }
    ToolTip.visible: tooltip !== "" && ma.containsMouse
    ToolTip.text: tooltip
    ToolTip.delay: 700
}
