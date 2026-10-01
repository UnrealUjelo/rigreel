import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Head (and some neck) turns towards the camera or another character, on top of the animation.
Section {
    id: sec
    title: "Look at"
    icon: "eye"
    Store { id: s }
    readonly property var a: s.actor
    readonly property var la: a ? a.lookat : ({})
    visible: !!a && a.kind !== "object"
    function set(f) { s.cmd("lookat_set", { fields: f }) }

    RowLayout {
        spacing: 12
        Toggle { text: "Enabled"; checked: !!sec.la.enabled; onToggled: (v) => sec.set({ enabled: v }) }
        Choice {
            Layout.fillWidth: true
            value: sec.la.kind || "camera"
            options: [{ v: "camera", label: "the camera" }, { v: "actor", label: "a character" }]
            onChosen: (v) => sec.set({ kind: v })
        }
    }
    Field {
        visible: sec.la.kind === "actor"
        label: "Character"
        Choice {
            Layout.fillWidth: true
            value: sec.la.target || 0
            options: [{ v: 0, label: "—" }].concat(s.actors.filter(o => sec.a && o.id !== sec.a.id).map(o => ({ v: o.id, label: N.actorLabel(o) })))
            onChosen: (v) => sec.set({ target: v })
        }
    }
    SliderRow { channel: sec.a ? "actor:" + sec.a.id + "/lookat_weight" : ""; label: "Strength"; value: sec.la.weight || 0; onEdited: (v) => sec.set({ weight: v }) }
    SliderRow { label: "Max angle"; from: 10; to: 120; step: 1; value: sec.la.max_deg || 75; format: (v) => v.toFixed(0) + "°"; onEdited: (v) => sec.set({ max_deg: v }) }
    SliderRow { label: "Smoothing"; from: 0.02; to: 1; value: sec.la.smooth || 0.12; onEdited: (v) => sec.set({ smooth: v }) }
    SliderRow { label: "Neck share"; value: sec.la.neck_share || 0.35; onEdited: (v) => sec.set({ neck_share: v }) }
    Toggle { text: "Flip forward (if the head turns away)"; checked: !!sec.la.flip; onToggled: (v) => sec.set({ flip: v }) }
}
