import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// The clip / cut selected in the timeline.
Section {
    id: sec
    Store { id: s }
    readonly property var ref: s.clipSel
    readonly property var tr: ref ? s.track(ref.track) : null
    readonly property var cut: tr && tr.kind === "camera" ? N.find(tr.cuts, c => c.id === ref.id) : null
    readonly property var clip: tr && tr.kind !== "camera" ? N.find(tr.clips, c => c.id === ref.id) : null
    readonly property bool pose: !!tr && tr.kind === "pose"
    visible: !!cut || !!clip
    title: cut ? "Cut · " + s.cameraName(cut.cam) : pose ? "Pose clip · " + (clip ? clip.keys.length : 0) + " keys" : clip ? "Clip · " + (clip.name ? N.motionLabel(clip.name).label : clip.mot) : "Clip"
    icon: cut ? "video" : pose ? "crosshair" : "film"
    function upd(f) { s.cmd("update_clip", { track: tr.idx, id: clip.id, fields: f }) }

    // camera cut
    Field { visible: !!sec.cut; label: "At frame"; NumField { Layout.fillWidth: true; step: 1; value: sec.cut ? sec.cut.start : 0; onEdited: (v) => s.cmd("update_cut", { track: sec.tr.idx, id: sec.cut.id, start: Math.round(v) }) } }
    Field {
        visible: !!sec.cut
        label: "Camera"
        Choice { Layout.fillWidth: true; value: sec.cut ? sec.cut.cam : 0; options: s.cameras.map(c => ({ v: c.i, label: c.name })); onChosen: (v) => s.cmd("update_cut", { track: sec.tr.idx, id: sec.cut.id, cam: v }) }
    }
    RowLayout {
        visible: !!sec.cut
        Btn { kind: "ghost"; text: "Go to cut"; onClicked: s.send("seq_seek", { t: sec.cut.start }) }
        Btn { kind: "danger"; icon: "trash-2"; text: "Delete"; onClicked: { s.cmd("remove_cut", { track: sec.tr.idx, id: sec.cut.id }); s.send("select_clip", {}) } }
    }

    // animation / pose clip
    Field { visible: !!sec.clip; label: "Start"; NumField { Layout.fillWidth: true; step: 1; value: sec.clip ? sec.clip.start : 0; onEdited: (v) => sec.upd({ start: Math.round(v) }) } }
    Field { visible: !!sec.clip; label: "Duration"; NumField { Layout.fillWidth: true; step: 1; value: sec.clip ? sec.clip.dur : 0; onEdited: (v) => sec.upd({ dur: Math.max(1, v) }) } }
    Field { visible: !!sec.clip && !sec.pose; label: "Clip offset"; NumField { Layout.fillWidth: true; step: 1; value: sec.clip ? sec.clip.offset || 0 : 0; onEdited: (v) => sec.upd({ offset: v }) } }
    SliderRow { visible: !!sec.clip && !sec.pose; label: "Speed"; from: -3; to: 3; step: 0.05; value: sec.clip ? (sec.clip.speed === undefined ? 1 : sec.clip.speed) : 1; format: (v) => v.toFixed(2) + "×"; onEdited: (v) => sec.upd({ speed: v }) }
    SliderRow { visible: !!sec.clip && !sec.pose; label: "Blend in"; from: 0; to: 120; step: 1; value: sec.clip ? (sec.clip.blend === undefined ? 10 : sec.clip.blend) : 10; format: (v) => v.toFixed(0) + " f"; onEdited: (v) => sec.upd({ blend: v }) }
    SliderRow { visible: sec.pose; label: "Strength"; value: sec.clip ? (sec.clip.weight === undefined ? 1 : sec.clip.weight) : 1; onEdited: (v) => sec.upd({ weight: v }) }
    Field {
        visible: sec.pose
        label: "Fade in / out"
        NumField { Layout.fillWidth: true; step: 1; value: sec.clip ? sec.clip.fade_in || 0 : 0; onEdited: (v) => sec.upd({ fade_in: Math.round(v) }) }
        NumField { Layout.fillWidth: true; step: 1; value: sec.clip ? sec.clip.fade_out || 0 : 0; onEdited: (v) => sec.upd({ fade_out: Math.round(v) }) }
    }
    RowLayout {
        visible: !!sec.clip
        spacing: 14
        Toggle { text: "Loop"; checked: !!sec.clip && !!sec.clip.loop; onToggled: (v) => sec.upd({ loop: v }) }
        Toggle { visible: sec.pose; text: "Additive"; checked: !!sec.clip && sec.clip.mode === "additive"; onToggled: (v) => sec.upd({ mode: v ? "additive" : "override" }) }
    }
    Field {
        visible: !!sec.clip && !sec.pose
        label: "Root motion"
        Text { text: sec.clip && sec.clip.rm ? "travels " + (sec.clip.rm_dist || 0).toFixed(2) + " m" : "in place"; color: sec.clip && sec.clip.rm ? Theme.amber : Theme.muted; font.pixelSize: Theme.smallSize }
        Btn {
            text: s.live && s.st.baking ? "baking… " + s.st.baking.frame + "/" + s.st.baking.n : sec.clip && sec.clip.rm ? "Re-bake from here" : "Bake travel"
            enabled: !(s.live && s.st.baking)
            tooltip: "Play the clip once, measure how far the feet carry the body, then move the character along that path"
            onClicked: s.cmd("bake_root_motion", { track: sec.tr.idx, id: sec.clip.id })
        }
        Btn { visible: !!sec.clip && !!sec.clip.rm; kind: "ghost"; text: "In place"; onClicked: s.cmd("bake_root_motion", { track: sec.tr.idx, id: sec.clip.id, clear: true }) }
    }
    Flow {
        visible: !!sec.clip && sec.pose
        Layout.fillWidth: true
        spacing: 3
        Repeater {
            model: sec.clip && sec.pose ? sec.clip.keys : []
            Chip { required property var modelData; text: "@" + (sec.clip.start + modelData); tooltip: "Go to key"; onClicked: s.send("seq_seek", { t: sec.clip.start + modelData }) }
        }
    }
    Flow {
        visible: !!sec.clip
        Layout.fillWidth: true
        spacing: 4
        Btn { visible: !sec.pose; text: "Fit to length"; enabled: !!sec.clip && !!sec.clip.endframe; onClicked: sec.upd({ dur: (sec.clip.endframe || sec.clip.dur) / Math.max(0.01, Math.abs(sec.clip.speed || 1)) }) }
        Btn { kind: "ghost"; text: "Go to start"; onClicked: s.send("seq_seek", { t: sec.clip.start }) }
        Btn { kind: "danger"; icon: "trash-2"; text: "Delete"; onClicked: { s.cmd("remove_clip", { track: sec.tr.idx, id: sec.clip.id }); s.send("select_clip", {}) } }
    }
}
