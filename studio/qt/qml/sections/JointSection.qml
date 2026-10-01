import QtQuick
import QtQuick.Layouts
import Director

// The selected bone: rotation sliders (SFM's control sliders), key / reset / mirror, pose strength.
Section {
    id: sec
    Store { id: s }
    readonly property var a: s.actor
    readonly property string joint: a && a.pose ? (a.pose.joint || "") : ""
    readonly property var e: a && a.pose && a.pose.euler ? a.pose.euler : [0, 0, 0]
    visible: !!a && a.kind !== "object"
    title: joint ? "Bone · " + joint : "Bone"
    icon: "bone"
    function rot(i, v) { const n = [e[0], e[1], e[2]]; n[i] = v; s.cmd("set_joint_euler", { name: joint, euler: n }) }

    Text {
        visible: !sec.joint
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: studio.standalone ? "Pick a bone in the list above or click its dot in the picture (Overlays ▸ Skeleton shows them). Then drag the sliders or the rings; with Move (W) a hand or foot drags its whole limb."
                                : "Pick a bone in the list above, or Ctrl+click a bone dot in the picture (Edit in picture). Then drag the sliders - or rotate it with E."
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
    SliderRow { visible: !!sec.joint; label: "Rotate X"; from: -180; to: 180; step: 0.5; value: sec.e[0]; fill: "#7a3434"; format: (v) => v.toFixed(1) + "°"; onEdited: (v) => sec.rot(0, v) }
    SliderRow { visible: !!sec.joint; label: "Rotate Y"; from: -180; to: 180; step: 0.5; value: sec.e[1]; fill: "#3d6e32"; format: (v) => v.toFixed(1) + "°"; onEdited: (v) => sec.rot(1, v) }
    SliderRow { visible: !!sec.joint; label: "Rotate Z"; from: -180; to: 180; step: 0.5; value: sec.e[2]; fill: "#34507a"; format: (v) => v.toFixed(1) + "°"; onEdited: (v) => sec.rot(2, v) }
    Flow {
        visible: !!sec.joint
        Layout.fillWidth: true
        spacing: 4
        Btn { kind: "primary"; icon: "key"; text: "Key pose @ " + s.t; tooltip: "Key every edited bone at the playhead (K)"; onClicked: s.cmd("keyframe_pose", { t: s.t }) }
        Btn { text: "Reset"; tooltip: "This bone back to the animation"; onClicked: s.cmd("reset_joint") }
        Btn { text: "Mirror"; enabled: /^[LRlr]_/.test(sec.joint); tooltip: "Copy this rotation to the other side"; onClicked: s.cmd("mirror_joint") }
        Btn { kind: "ghost"; text: "Remove from pose"; onClicked: s.cmd("remove_joint") }
        Btn { kind: "ghost"; text: "Rotate in picture (E)"; onClicked: { s.send("gizmo", { target: "bone", tool: "rotate", enabled: true }); if (!studio.standalone) s.send("edit_mode", { value: true }); studio.focusGame() } }
        Btn { visible: studio.standalone && !!(sec.a && sec.a.pose && sec.a.pose.ik); kind: "ghost"; text: "Drag the limb (W)"; tooltip: "Move this hand or foot in the picture: the arm or leg follows (IK)"; onClicked: { s.send("gizmo", { target: "bone", tool: "move", enabled: true }); studio.focusGame() } }
    }
    SliderRow {
        label: "Pose strength"
        value: sec.a && sec.a.pose ? sec.a.pose.weight : 1
        onEdited: (v) => s.cmd("pose_weight", { value: v })
    }
    Text {
        text: sec.a && sec.a.pose ? sec.a.pose.posed + " bone" + (sec.a.pose.posed === 1 ? "" : "s") + " in the working pose" + (s.live && s.st.motion_edit && s.range ? (s.st.relative_edits === false ? " · K holds this pose over the time selection" : " · K adds this change to the motion over the time selection") : "") : ""
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
}
