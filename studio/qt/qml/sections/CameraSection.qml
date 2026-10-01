import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// A scene camera: lens (field of view / focal length), roll, depth of field, handheld shake, how it moves.
Section {
    id: sec
    Store { id: s }
    readonly property var c: s.camera
    visible: !!c
    title: c ? "Camera · " + c.name : "Camera"
    icon: "video"
    function upd(f) { s.cmd("cam_update", { i: c.i, fields: f }) }
    readonly property var pathTrack: c ? N.find(s.seq.tracks, t => t.kind === "cammove" && t.cam === c.i) : null

    Field { label: "Name"; TextBox { Layout.fillWidth: true; text: sec.c ? sec.c.name : ""; onCommitted: (t) => sec.upd({ name: t }) } }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Btn { kind: sec.c && sec.c.live ? "primary" : ""; icon: "video"; text: sec.c && sec.c.live ? "Looking through it" : "Look through"; onClicked: s.send("cam_live", { i: sec.c.live ? 0 : sec.c.i }) }
        Btn { icon: "plus"; text: "Cut @ " + s.t; tooltip: "The film cuts to this camera at the playhead"; onClicked: s.cmd("add_cut", { cam: sec.c.i, t: s.t }) }
        Btn { icon: "key"; kind: "primary"; text: "Key @ " + s.t; tooltip: "Store position, rotation and lens here (M). Two keys = a camera move."; onClicked: s.cmd("key_camera", { i: sec.c.i, t: s.t }) }
        Btn { icon: "plane"; text: "Fly"; tooltip: "Look through it and fly it: WASD · Q/E · mouse · F3 keys it · F1 back"; onClicked: { s.send("cam_live", { i: sec.c.i }); s.send("cam_fly", { value: true }); studio.focusGame() } }
        Btn { kind: "ghost"; text: "Recapture"; tooltip: "Copy the current view into this camera"; onClicked: s.cmd("cam_recapture", { i: sec.c.i }) }
        Btn { kind: "danger"; icon: "trash-2"; tooltip: "Delete this camera (its cuts and move keys go with it)"; onClicked: s.cmd("cam_remove", { i: sec.c.i }) }
    }
    SliderRow { channel: sec.c ? "cam:" + sec.c.i + "/fov" : ""; label: "Field of view"; from: 10; to: 120; step: 0.5; value: sec.c ? sec.c.fov : 50; format: (v) => v.toFixed(1) + "°"; onEdited: (v) => sec.upd({ fov: v }) }
    Field {
        label: "Focal length"
        NumField { implicitWidth: 64; step: 1; suffix: " mm"; value: sec.c ? N.fovToMm(sec.c.fov) : 35; onEdited: (v) => sec.upd({ fov: N.mmToFov(v) }) }
        Repeater {
            model: [18, 24, 35, 50, 85]
            Chip { required property int modelData; text: String(modelData); checked: !!sec.c && Math.abs(N.fovToMm(sec.c.fov) - modelData) < 0.6; onClicked: sec.upd({ fov: N.mmToFov(modelData) }) }
        }
    }
    SliderRow { channel: sec.c ? "cam:" + sec.c.i + "/roll" : ""; label: "Roll (Dutch)"; from: -60; to: 60; step: 0.5; value: sec.c ? sec.c.roll || 0 : 0; format: (v) => v.toFixed(1) + "°"; onEdited: (v) => sec.upd({ roll: v }) }
    Toggle {
        text: "Cinematic depth of field (focus on the look-at target)"
        checked: !!sec.c && sec.c.dof_f !== null && sec.c.dof_f !== undefined
        onToggled: (v) => s.cmd("cam_dof_params", v ? { i: sec.c.i, f: 2.0, focus: "target" } : { i: sec.c.i, f: false, focus: false })
    }
    SliderRow {
        visible: !!sec.c && sec.c.dof_f !== null && sec.c.dof_f !== undefined
        label: "Aperture f/"; from: 1.2; to: 16; step: 0.1; value: sec.c && sec.c.dof_f ? sec.c.dof_f : 2
        channel: sec.c ? "cam:" + sec.c.i + "/dof_f" : ""
        onEdited: (v) => s.cmd("cam_dof_params", { i: sec.c.i, f: v })
    }
    Field {
        visible: !!sec.c && sec.c.dof_f !== null && sec.c.dof_f !== undefined
        label: "Focus"
        Btn { text: "Target"; kind: sec.c && (sec.c.dof_focus === "target" || sec.c.dof_focus === null || sec.c.dof_focus === undefined) ? "primary" : ""; onClicked: s.cmd("cam_dof_params", { i: sec.c.i, focus: "target" }) }
        NumField { Layout.fillWidth: true; step: 0.1; suffix: " m"; value: sec.c && typeof sec.c.dof_focus === "number" ? sec.c.dof_focus : 3; onEdited: (v) => s.cmd("cam_dof_params", { i: sec.c.i, focus: v }) }
        KeyButton { channel: sec.c ? "cam:" + sec.c.i + "/dof_focus" : "" }
    }
    RowLayout {
        visible: !!sec.c && sec.c.dof_f !== null && sec.c.dof_f !== undefined
        Btn { text: "Focus selected"; enabled: !!s.actor; onClicked: s.cmd("cam_focus_selection", { i: sec.c.i }) }
        Btn { text: "Focus + key"; enabled: !!s.actor; onClicked: s.cmd("cam_focus_selection", { i: sec.c.i, key: true, t: s.t }) }
    }
    Toggle { text: "The game's own depth of field"; checked: !!sec.c && sec.c.dof !== false; onToggled: (v) => s.cmd("cam_dof", { i: sec.c.i, value: v }) }
    Toggle { text: "Handheld shake"; checked: !!sec.c && !!sec.c.shake; onToggled: (v) => sec.upd({ shake: v ? { amp: 0.01, rot: 0.6, freq: 1.4 } : false }) }
    SliderRow { visible: !!sec.c && !!sec.c.shake; label: "Shake · move"; from: 0; to: 0.1; step: 0.001; value: sec.c && sec.c.shake ? sec.c.shake.amp : 0; format: (v) => (v * 100).toFixed(1) + " cm"; onEdited: (v) => sec.upd({ shake: Object.assign({}, sec.c.shake, { amp: v }) }) }
    SliderRow { visible: !!sec.c && !!sec.c.shake; label: "Shake · turn"; from: 0; to: 5; step: 0.05; value: sec.c && sec.c.shake ? sec.c.shake.rot : 0; format: (v) => v.toFixed(2) + "°"; onEdited: (v) => sec.upd({ shake: Object.assign({}, sec.c.shake, { rot: v }) }) }
    SliderRow { visible: !!sec.c && !!sec.c.shake; label: "Shake · speed"; from: 0.2; to: 6; step: 0.1; value: sec.c && sec.c.shake ? sec.c.shake.freq : 1; format: (v) => v.toFixed(1) + " Hz"; onEdited: (v) => sec.upd({ shake: Object.assign({}, sec.c.shake, { freq: v }) }) }
    Field {
        visible: !!sec.pathTrack
        label: "Path aims at"
        Choice {
            Layout.fillWidth: true
            value: sec.pathTrack ? sec.pathTrack.lookat_name || "" : ""
            options: [{ v: "", label: "nothing (keyed rotation)" }].concat(s.actors.map(o => ({ v: o.name, label: N.actorLabel(o) })))
            onChosen: (v) => { const o = N.find(s.actors, x => x.name === v); s.cmd("cammove_lookat", { track: sec.pathTrack.idx, addr: o ? o.id : false }) }
        }
    }
    Vec3Field { visible: !!sec.c && (sec.c.mode === "static" || sec.c.mode === "lookat"); label: "Position"; value: sec.c ? sec.c.pos : [0, 0, 0]; onEdited: (v) => sec.upd({ pos: v }) }
    Btn { visible: !!sec.c && sec.c.mode !== "static"; kind: "ghost"; text: "Make it a fixed shot"; tooltip: "Freeze this camera where it is now (stops following / orbiting)"; onClicked: sec.upd({ mode: "static" }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "follow"; label: "Behind"; from: -4; to: 6; step: 0.05; value: sec.c && sec.c.follow ? -sec.c.follow[2] : 2.2; format: (v) => v.toFixed(2) + " m"; onEdited: (v) => sec.upd({ follow: [sec.c.follow ? sec.c.follow[0] : 0.9, sec.c.follow ? sec.c.follow[1] : 1.5, -v] }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "follow"; label: "Side"; from: -4; to: 4; step: 0.05; value: sec.c && sec.c.follow ? sec.c.follow[0] : 0.9; format: (v) => v.toFixed(2) + " m"; onEdited: (v) => sec.upd({ follow: [v, sec.c.follow ? sec.c.follow[1] : 1.5, sec.c.follow ? sec.c.follow[2] : -2.2] }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "follow"; label: "Height"; from: 0.2; to: 5; step: 0.05; value: sec.c && sec.c.follow ? sec.c.follow[1] : 1.5; format: (v) => v.toFixed(2) + " m"; onEdited: (v) => sec.upd({ follow: [sec.c.follow ? sec.c.follow[0] : 0.9, v, sec.c.follow ? sec.c.follow[2] : -2.2] }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "follow"; label: "Smoothing"; from: 0; to: 0.9; step: 0.01; value: sec.c ? sec.c.smooth || 0.12 : 0.12; onEdited: (v) => sec.upd({ smooth: v }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "orbit"; label: "Radius"; from: 0.3; to: 15; value: sec.c ? sec.c.radius : 2; onEdited: (v) => sec.upd({ radius: v }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "orbit"; label: "Height"; from: -2; to: 6; value: sec.c ? sec.c.height : 1.5; onEdited: (v) => sec.upd({ height: v }) }
    SliderRow { visible: !!sec.c && sec.c.mode === "orbit"; label: "Orbit speed"; from: -2; to: 2; value: sec.c ? sec.c.speed : 0.2; onEdited: (v) => sec.upd({ speed: v }) }
    Vec3Field { visible: !!sec.c && sec.c.mode !== "static"; label: "Aim offset"; value: sec.c ? sec.c.offset : [0, 1.4, 0]; onEdited: (v) => sec.upd({ offset: v }) }
    Field {
        visible: !!sec.c && sec.c.mode !== "static"
        label: "Target"
        Text { Layout.fillWidth: true; text: sec.c && sec.c.target_name ? sec.c.target_name : "—"; color: Theme.text; font.pixelSize: Theme.fontSize; elide: Text.ElideRight }
        Btn { text: "Switch · 1 s"; enabled: !!s.actor; tooltip: "Glide over to the selected character in one second"; onClicked: s.cmd("cam_target_selection", { i: sec.c.i, addr: s.actor.id, duration: 1 }) }
    }
}
