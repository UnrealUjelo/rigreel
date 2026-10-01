import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Play a clip from an open animation set on the selected character (and put it on the timeline).
Section {
    id: sec
    title: "Play a clip"
    icon: "film"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a && a.kind !== "object"
    property int bankSel: 0
    property int motSel: -1
    readonly property var bank: a ? (N.find(a.banks, b => b.id === bankSel) || (a.banks.length ? a.banks[a.banks.length - 1] : null)) : null
    readonly property var motions: bank && s.dt && s.dt.motions ? (s.dt.motions[String(bank.id)] || []) : []
    readonly property var layerInfo: a ? N.find(a.layers, l => l.idx === Ui.layer) : null

    Text {
        visible: !!sec.a && sec.a.banks.length === 0
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: "Open an animation set in the Asset Browser ▸ Animations first."
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
    Field {
        visible: !!sec.a && sec.a.banks.length > 0
        label: "Set"
        Choice {
            Layout.fillWidth: true
            value: sec.bank ? sec.bank.id : 0
            options: sec.a ? sec.a.banks.map(b => ({ v: b.id, label: b.name.replace(".motlist", "") + (b.ready ? "" : " (loading)") })) : []
            onChosen: (v) => { sec.bankSel = v; sec.motSel = -1 }
        }
    }
    Field {
        visible: !!sec.a && sec.a.banks.length > 0
        label: "Clip"
        Choice {
            Layout.fillWidth: true
            value: sec.motSel
            placeholder: sec.motions.length ? "Choose a clip…" : "loading…"
            options: sec.motions.map(m => ({ v: m.id, label: N.motionLabel(m.name).label }))
            onChosen: (v) => sec.motSel = v
        }
    }
    Field {
        label: "Layer"
        Choice {
            Layout.fillWidth: true
            value: Ui.layer
            options: Array.from({ length: Math.max(1, sec.a ? sec.a.layer_count : 1) }, (_, i) => ({ v: i, label: i === 0 ? "0 · full body" : i === 1 ? "1 · upper body" : String(i) }))
            onChosen: (v) => Ui.layer = v
        }
    }
    SliderRow { label: "Blend"; from: 0; to: 60; step: 1; value: Ui.blend; format: (v) => v.toFixed(0) + " f"; onEdited: (v) => Ui.blend = v }
    SliderRow { label: "Speed"; from: -2; to: 3; step: 0.05; value: Ui.speed; format: (v) => v.toFixed(2) + "×"; onEdited: (v) => { Ui.speed = v; s.send("set_speed", { layer: Ui.layer, value: v }) } }
    SliderRow {
        visible: !!sec.layerInfo && !!sec.layerInfo.endframe
        label: "Frame"; from: 0; to: sec.layerInfo && sec.layerInfo.endframe ? sec.layerInfo.endframe : 1; step: 1
        value: sec.layerInfo ? (sec.layerInfo.frame || 0) : 0
        onEdited: (v) => s.send("set_frame", { layer: Ui.layer, value: v })
    }
    RowLayout {
        Btn { kind: "primary"; icon: "play"; text: "Play"; enabled: !!sec.bank && sec.motSel >= 0; onClicked: s.send("play", { bank: sec.bank.id, mot: sec.motSel, layer: Ui.layer, blend: Ui.blend, speed: Ui.speed }) }
        Btn { visible: studio.standalone && !!sec.layerInfo; icon: "square"; text: "Stop"; tooltip: "Stop the played clip (the timeline and the idle take over)"; onClicked: s.send("play_stop") }
        Btn {
            icon: "plus"; text: "Timeline"; enabled: !!sec.bank && sec.motSel >= 0
            tooltip: "Add to the timeline at the playhead"
            onClicked: {
                const m = N.find(sec.motions, x => x.id === sec.motSel)
                s.cmd("add_clip", { bank: sec.bank.id, mot: sec.motSel, layer: Ui.layer, blend: Ui.blend, speed: Ui.speed, name: m ? m.name : "", endframe: m ? m.endframe : 0 })
            }
        }
    }
}
