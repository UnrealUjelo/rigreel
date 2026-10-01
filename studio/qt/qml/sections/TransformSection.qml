import QtQuick
import QtQuick.Layouts
import Director

// Where the selected character / prop is: position, facing (or rotation + scale for props), size, and the
// director's switches (AI off, lock root, pause, hide).
Section {
    id: sec
    title: "Transform"
    icon: "move-3d"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a

    Vec3Field { label: "Position"; value: sec.a ? sec.a.pos : [0, 0, 0]; step: 0.01; suffix: ""; onEdited: (v) => s.cmd("set_transform", { pos: v }) }
    Vec3Field {
        visible: !!sec.a && sec.a.kind === "object"
        label: "Rotation"; value: sec.a ? sec.a.euler : [0, 0, 0]; step: 1
        onEdited: (v) => s.cmd("set_transform", { euler: v })
    }
    Vec3Field {
        visible: !!sec.a && sec.a.kind === "object"
        label: "Scale"; value: sec.a ? (sec.a.scale3 || [sec.a.scale || 1, sec.a.scale || 1, sec.a.scale || 1]) : [1, 1, 1]; step: 0.01
        onEdited: (v) => s.cmd("set_scale", { scale: v })
    }
    SliderRow {
        visible: !!sec.a && sec.a.kind !== "object"
        label: "Facing"; from: -180; to: 180; step: 1
        value: sec.a ? sec.a.euler[1] : 0
        format: (v) => v.toFixed(0) + "°"
        onEdited: (v) => s.cmd("set_transform", { euler: [sec.a.euler[0], v, sec.a.euler[2]] })
    }
    SliderRow {
        visible: !!sec.a && sec.a.kind !== "object"
        label: "Size"; from: 0.2; to: 3; step: 0.01
        channel: sec.a ? "actor:" + sec.a.id + "/scale" : ""
        value: sec.a ? (sec.a.scale || 1) : 1
        format: (v) => Math.round(v * 100) + " %"
        onEdited: (v) => s.cmd("set_scale", { scale: [v, v, v] })
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Btn {
            visible: !!sec.a && sec.a.kind !== "object"
            icon: "gamepad-2"
            readonly property bool on: !!(s.st.drive && s.st.drive.on && s.st.drive.actor === (sec.a ? sec.a.id : -1))
            text: on ? "Stop driving" : "Drive"
            kind: on ? "primary" : ""
            tooltip: "Possess this character: WASD walks with real locomotion clips, Shift jogs, R records into the timeline, Esc stops"
            onClicked: { s.send("drive", { value: !on, addr: sec.a.id }); if (!on) studio.focusGame() }
        }
        Btn { text: "To camera"; tooltip: "Put it where the view is"; onClicked: s.cmd("move_to_camera") }
        Btn { text: "Face view"; onClicked: s.cmd("face_camera") }
        Btn { text: "Turn 180°"; onClicked: s.cmd("flip_facing") }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 14
        Toggle { visible: !!sec.a && sec.a.has_fsm; text: "AI off"; tooltip: "The character stops acting on its own"; checked: !!sec.a && sec.a.puppet; onToggled: (v) => s.send("set_puppet", { value: v }) }
        Toggle { text: "Lock root"; tooltip: "Animations play in place"; checked: !!sec.a && sec.a.root_lock; onToggled: (v) => s.cmd("set_root_lock", { value: v }) }
        Toggle { visible: !!sec.a && sec.a.kind !== "object"; text: "Paused"; checked: !!sec.a && sec.a.paused; onToggled: (v) => s.send("set_paused", { value: v }) }
        Toggle { text: "Hidden"; checked: !!sec.a && !!sec.a.hidden; onToggled: (v) => s.send("actor_visible", { addr: sec.a.id, value: !v }) }
        KeyButton { channel: sec.a ? "actor:" + sec.a.id + "/visible" : "" }
    }
    RowLayout {
        Btn { icon: "key"; kind: "primary"; text: "Key position @ " + s.t; tooltip: "Record position + facing on the Position track"; onClicked: s.cmd("key_transform", { t: s.t }) }
        Btn { visible: !studio.standalone && !!sec.a && !sec.a.spawned; kind: "ghost"; text: "Release"; tooltip: "Give this character back to the game"; onClicked: s.send("release_actor", { addr: sec.a.id }) }
        Btn { visible: !!sec.a && sec.a.spawned; kind: "danger"; icon: "trash-2"; text: "Remove"; tooltip: "Take it out of the film with its tracks (Delete) · Ctrl+Z brings it back"; onClicked: s.cmd("destroy_object", { addr: sec.a.id }) }
    }
}
