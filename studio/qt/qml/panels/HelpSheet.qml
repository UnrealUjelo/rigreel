import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// Help ▸ Keyboard Shortcuts, plus the SFM workflow in five lines.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg

    readonly property var groups: [
        { title: "Editors (like SFM)", keys: [["F2", "Clip Editor — shots, cuts, clips, sound"], ["F3", "Motion Editor — keys + time selection with falloff"], ["F4", "Graph Editor — curves"], ["Tab", "Back to the last editor"]] },
        { title: "Manipulators", keys: [["Q", "Select"], ["W", "Move"], ["E", "Rotate"], ["R", "Scale"], ["X", "World / local axes"]] },
        { title: "Playback", keys: [["Space", "Play / pause"], ["← / →", "Previous / next frame (Shift: 10)"], ["↑ / ↓", "Previous / next key"], ["Home / End", "Start / end"], ["I / O", "Time selection start / end"], ["Alt+O", "Clear the time selection"], ["L", "Loop"]] },
        { title: "Keys and clips", keys: [["K", "Key the selection"], ["M", "Key the camera"], ["Shift+K", "Auto-key"], ["Ctrl+B", "Split the clip at the playhead"], ["[ / ]", "Trim clip start / end to the playhead"], ["Ctrl+D", "Duplicate clip"], ["Ctrl+C / V", "Copy / paste clip or keys"], ["Delete", "Remove keys / clip"], ["Ctrl+A", "Select all keys"], ["Esc", "Deselect"]] },
        { title: "Scene", keys: [["C", "New camera from the view"], ["Shift+B", "Skeleton overlay"], ["Ctrl+Shift+F", "Freeze the world"], ["Ctrl+R", "Rescan the game world"]] },
        { title: "File", keys: [["Ctrl+N / O / S", "New / open / save project"], ["Ctrl+Shift+S", "Save as"], ["Ctrl+I", "Import an animation"], ["Ctrl+Shift+M", "Export the movie"]] },
        { title: "Timeline mouse", keys: [["Wheel", "Zoom time"], ["Ctrl+wheel / middle-drag", "Pan time"], ["Shift+wheel", "Scroll tracks"], ["Drag a key", "Retime (Alt: scale the selection)"], ["Drag a clip / its edge", "Move / trim"], ["Double-click a shot", "Enter the shot"], ["Right-click", "Menu for what is under the mouse"]] },
        { title: "In the game window", keys: [["F1", "Mouse to the game / back to the Studio (anywhere)"], ["F2", "Edit in the picture on / off"], ["F3", "Key the flying camera"], ["WASD · Q/E", "Fly / drive"], ["Shift / Ctrl", "Fast / slow"], ["RMB drag", "Look around (edit mode)"], ["MMB drag", "Orbit · Shift: pan"], ["F", "Frame the selection"], ["Ctrl+drag", "Turn the character"], ["P · Enter", "Walk path: add points · done"], ["R", "Record while driving"], ["Insert", "REFramework cursor (manipulator handles)"]] },
    ]

    ScrollView {
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: 14
            Text { text: "Working like Source Filmmaker"; color: Theme.textBright; font.pixelSize: 16; font.bold: true }
            Text {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: Theme.text; font.pixelSize: Theme.fontSize; lineHeight: 1.3
                text: "1.  Look around with the Work Camera (View ▸ Viewport Camera): it flies freely and is never rendered.\n" +
                      "2.  Press C to turn the view into a scene camera. Make shots on the Film track (I / O, then + Shot) and give each its camera.\n" +
                      "3.  Bring in characters and props from the Asset Browser; they become animation sets in the Animation Set Editor.\n" +
                      "4.  Animate: clips in the Clip Editor, poses with the bone sliders (K keys them), refine timing in the Motion Editor and curves in the Graph Editor.\n" +
                      "5.  Switch the viewport to the Scene Camera to watch the edit, then File ▸ Export ▸ Movie."
            }
            GridLayout {
                columns: 2
                columnSpacing: 24
                rowSpacing: 14
                Layout.fillWidth: true
                Repeater {
                    model: root.groups
                    ColumnLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 3
                        Text { text: modelData.title; color: Theme.amber; font.pixelSize: Theme.fontSize; font.bold: true }
                        Repeater {
                            model: modelData.keys
                            RowLayout {
                                required property var modelData
                                spacing: 10
                                Rectangle {
                                    implicitWidth: Math.max(62, k.implicitWidth + 12); implicitHeight: 20; radius: 3
                                    color: "#2b2b31"; border.color: "#44444c"
                                    Text { id: k; anchors.centerIn: parent; text: parent.parent.modelData[0]; color: Theme.textBright; font.family: Theme.mono; font.pixelSize: 11 }
                                }
                                Text { text: parent.modelData[1]; color: Theme.text; font.pixelSize: Theme.smallSize; Layout.fillWidth: true; wrapMode: Text.WordWrap }
                            }
                        }
                    }
                }
            }
        }
    }
}
