import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Walk path: click points on the floor, the character walks / jogs along them with real locomotion clips.
Section {
    id: sec
    title: tr ? "Walk path · " + (tr.length || 0).toFixed(1) + " m" : "Walk path"
    icon: "route"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a && a.kind !== "object"
    open: !!tr
    readonly property var tr: a ? N.find(s.seq.tracks, t => t.kind === "path" && t.actor === a.id) : null
    function upd(f) { s.cmd("path_update", { track: tr.idx, fields: f }) }

    RowLayout {
        visible: !sec.tr
        Btn { kind: "primary"; icon: "route"; text: "Draw a walk path"; onClicked: { s.cmd("path_new", { addr: sec.a.id, start: s.t, gait: "walk" }); studio.focusGame() } }
        Btn { text: "Draw a jog path"; onClicked: { s.cmd("path_new", { addr: sec.a.id, start: s.t, gait: "jog" }); studio.focusGame() } }
    }
    Flow {
        visible: !!sec.tr
        Layout.fillWidth: true
        spacing: 4
        Btn { visible: !!sec.tr && sec.tr.drawing; kind: "primary"; text: "Done drawing"; onClicked: s.send("path_finish") }
        Btn { visible: !!sec.tr && !sec.tr.drawing; kind: "primary"; text: "Add points (P)"; onClicked: { s.cmd("path_new", { addr: sec.a.id, clear: false }); studio.focusGame() } }
        Btn { text: "Undo last point"; enabled: !!sec.tr && sec.tr.points > 0; onClicked: s.cmd("path_pop", { track: sec.tr.idx }) }
        Btn { text: "Reverse"; enabled: !!sec.tr && sec.tr.points > 1; onClicked: sec.upd({ reverse: true }) }
        Btn { kind: "danger"; icon: "trash-2"; text: "Remove path"; onClicked: s.cmd("path_remove", { track: sec.tr.idx }) }
    }
    Text {
        visible: !!sec.tr
        text: sec.tr ? sec.tr.points + " points · " + (sec.tr.clip_name ? N.motionLabel(sec.tr.clip_name).label : "no walk clip yet") : ""
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
    Field {
        visible: !!sec.tr
        label: "Gait"
        Chip { text: "Walk"; checked: !!sec.tr && sec.tr.gait !== "jog"; onClicked: sec.upd({ gait: "walk" }) }
        Chip { text: "Jog"; checked: !!sec.tr && sec.tr.gait === "jog"; onClicked: sec.upd({ gait: "jog" }) }
    }
    Field {
        visible: !!sec.tr
        label: "Starts at frame"
        NumField { Layout.fillWidth: true; step: 1; value: sec.tr ? sec.tr.start || 0 : 0; onEdited: (v) => sec.upd({ start: Math.round(v) }) }
    }
    SliderRow {
        visible: !!sec.tr
        label: "Speed"; from: 0.3; to: 5; step: 0.05
        value: sec.tr ? sec.tr.speed || 1.35 : 1.35
        format: (v) => v.toFixed(2) + " m/s" + (sec.tr && sec.tr.dur_pinned ? " (from duration)" : "")
        onEdited: (v) => sec.upd({ speed: v })
    }
    Field {
        visible: !!sec.tr
        label: "Duration"
        NumField { Layout.fillWidth: true; step: 0.1; suffix: " s"; value: sec.tr ? (sec.tr.dur || 0) / s.fps : 0; onEdited: (v) => sec.upd({ dur: Math.round(v * s.fps) }) }
    }
    SliderRow { visible: !!sec.tr; label: "Ease in / out"; from: 0; to: 0.45; step: 0.01; value: sec.tr ? sec.tr.ease || 0.08 : 0.08; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => sec.upd({ ease: v }) }
    RowLayout {
        visible: !!sec.tr
        Btn { kind: "ghost"; text: "Go to start"; onClicked: s.send("seq_seek", { t: sec.tr.start || 0 }) }
        Btn { kind: "ghost"; text: "Go to end"; onClicked: s.send("seq_seek", { t: (sec.tr.start || 0) + (sec.tr.dur || 0) }) }
    }
}
