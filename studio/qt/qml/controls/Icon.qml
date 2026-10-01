import QtQuick

// A Lucide stroke icon in any colour: Icon { name: "camera"; color: Theme.text }
Image {
    property string name: "circle-help"
    property color color: "#d8d8de"
    property int size: 16
    width: size; height: size
    sourceSize: Qt.size(size * 2, size * 2)
    source: name ? "image://icon/" + name + "/" + String(color).replace("#", "") : ""
    smooth: true
    asynchronous: false
}
