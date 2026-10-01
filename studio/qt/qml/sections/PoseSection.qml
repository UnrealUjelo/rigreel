import QtQuick
import QtQuick.Layouts
import Director

// Pose presets (SFM's "Presets" for an animation set): freeze parts of the current animation into the working
// pose, save poses into a library with thumbnails, apply or merge them, key the pose, export for Blender.
Section {
    id: sec
    title: "Pose presets"
    icon: "wand-sparkles"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a
    property string poseName: "pose1"
    property string saveFilter: "working"

    Field {
        label: "From animation"
        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: [["all", "Whole body"], ["upper", "Upper body"], ["lower", "Legs"], ["left_hand", "L fingers"], ["right_hand", "R fingers"], ["face", "Face"]]
                Chip { required property var modelData; text: modelData[1]; tooltip: "Freeze this part of the current animated pose into the working pose"; onClicked: s.cmd("capture_pose", { filter: modelData[0] }) }
            }
        }
    }
    RowLayout {
        Btn { kind: "primary"; icon: "key"; text: "Key pose @ " + s.t; enabled: !!sec.a && sec.a.pose && sec.a.pose.posed > 0; onClicked: s.cmd("keyframe_pose", { t: s.t }) }
        Btn { kind: "ghost"; text: "Load key here"; onClicked: s.cmd("load_key", { t: s.t }) }
        Btn { kind: "ghost"; text: "Clear"; onClicked: s.cmd("clear_pose") }
    }
    RowLayout {
        TextBox { Layout.fillWidth: true; text: sec.poseName; onTextChanged: sec.poseName = text }
        Choice {
            implicitWidth: 110
            value: sec.saveFilter
            options: [{ v: "working", label: "working pose" }, { v: "all", label: "whole body" }, { v: "upper", label: "upper body" }, { v: "left_hand", label: "L fingers" }, { v: "right_hand", label: "R fingers" }, { v: "fingers", label: "both hands" }]
            onChosen: (v) => sec.saveFilter = v
        }
        Btn {
            text: "Save"
            enabled: sec.saveFilter !== "working" || (sec.a && sec.a.pose && sec.a.pose.posed > 0)
            onClicked: { s.cmd("save_pose", { name: sec.poseName, filter: sec.saveFilter }); studio.poseThumbnail(sec.poseName) }
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: s.dt ? s.dt.poses : []
            Rectangle {
                id: card
                required property string modelData
                width: 96; height: 86; radius: 4
                color: cma.containsMouse ? Theme.bg3 : Theme.bg2
                border.color: Theme.border
                Image {
                    x: 3; y: 3; width: 90; height: 50
                    fillMode: Image.PreserveAspectCrop
                    source: studio.poseThumbUrl(card.modelData)
                    cache: false
                }
                Text { x: 5; y: 55; width: 86; text: card.modelData; color: Theme.text; font.pixelSize: 10; elide: Text.ElideRight }
                Row {
                    x: 3; y: 68; spacing: 2
                    Btn { text: "Apply"; implicitHeight: 16; onClicked: s.cmd("load_pose", { name: card.modelData }) }
                    Btn { kind: "ghost"; text: "Merge"; implicitHeight: 16; onClicked: s.cmd("load_pose", { name: card.modelData, merge: true }) }
                }
                MouseArea { id: cma; anchors.fill: parent; hoverEnabled: true; acceptedButtons: Qt.NoButton }
            }
        }
    }
    RowLayout {
        Btn { kind: "ghost"; text: "Export for Blender"; tooltip: "Rig + pose keys + travel keys as JSON in director/export (tools/anim_import/blender_import_director.py opens it)"; onClicked: s.cmd("export_anim", {}) }
        Btn { kind: "ghost"; text: "Open export folder"; onClicked: studio.openFolder("export") }
    }
}
