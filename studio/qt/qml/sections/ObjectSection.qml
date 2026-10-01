import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// A prop: attach it to a character's joint (weapon sockets, hands, head, hips), or remove it.
Section {
    id: sec
    title: "Prop"
    icon: "box"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a && a.kind === "object"
    property int who: 0
    property string joint: "R_Wep"
    readonly property var chars: s.actors.filter(o => sec.a && o.id !== sec.a.id && o.kind !== "object")
    readonly property int target: who || (chars.length ? chars[0].id : 0)
    readonly property var at: a ? a.attached : null

    Text { visible: !!sec.at; text: sec.at ? "On " + sec.at.actor_name + " · " + sec.at.joint : ""; color: Theme.amber; font.pixelSize: Theme.fontSize }
    Vec3Field { visible: !!sec.at; label: "Offset"; value: sec.at ? sec.at.pos : [0, 0, 0]; step: 0.005; onEdited: (v) => s.cmd("attach_offset", { pos: v }) }
    Vec3Field { visible: !!sec.at; label: "Tilt"; value: sec.at ? sec.at.euler : [0, 0, 0]; step: 1; onEdited: (v) => s.cmd("attach_offset", { euler: v }) }
    Btn { visible: !!sec.at; kind: "ghost"; text: "Detach"; onClicked: s.cmd("detach", {}) }

    Field {
        visible: !sec.at
        label: "Attach to"
        Choice {
            Layout.fillWidth: true
            value: sec.target
            placeholder: "no character"
            options: sec.chars.map(o => ({ v: o.id, label: N.actorLabel(o) }))
            onChosen: (v) => sec.who = v
        }
    }
    Field {
        visible: !sec.at
        label: "Joint"
        Choice {
            Layout.fillWidth: true
            value: sec.joint
            options: ["R_Wep", "L_Wep", "R_Hand", "L_Hand", "Head", "Hip", "Spine_2"].concat((s.dt ? s.dt.joints : []).filter(j => ["R_Wep", "L_Wep", "R_Hand", "L_Hand", "Head", "Hip", "Spine_2"].indexOf(j) < 0)).map(j => ({ v: j, label: j }))
            onChosen: (v) => sec.joint = v
        }
    }
    RowLayout {
        visible: !sec.at
        Btn { kind: "primary"; text: "Attach"; enabled: sec.target > 0; tooltip: "Snap the prop into the joint"; onClicked: s.cmd("attach", { actor: sec.target, joint: sec.joint }) }
        Btn { text: "Attach in place"; enabled: sec.target > 0; tooltip: "Attach but keep the prop where it is now"; onClicked: s.cmd("attach", { actor: sec.target, joint: sec.joint, keep: true }) }
    }
    Text {
        visible: !!sec.a && !sec.a.spawned
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: "This one belongs to the level. Moving it changes the room; Release hands it back."
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
}
