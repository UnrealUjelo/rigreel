import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// A spawned cast member: its look (costume), physics for hair / straps, damage skin, duplicate, remove.
Section {
    id: sec
    Store { id: s }
    readonly property var a: s.actor
    readonly property var spec: a ? a.cast : null
    visible: !!spec
    title: "Cast member" + (spec ? " · " + spec.name : "")
    icon: "user-plus"
    readonly property var entry: spec && spec.entry && spec.entry.presets ? spec.entry : spec && s.dt ? N.find(s.dt.cast, c => c.id === spec.id && c.tree === spec.tree) : null
    readonly property string cur: spec ? (spec.preset_name || "") : ""
    readonly property var looks: entry ? entry.presets.filter(p => !p.variant || p.name === cur) : []

    Text {
        text: (sec.entry ? sec.entry.name : (sec.spec ? sec.spec.id : "")) + " · " + (sec.cur || "look") + (sec.spec && sec.spec.pending > 0 ? " · loading " + sec.spec.pending + " part(s)…" : "")
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
    Field {
        visible: sec.looks.length > 1
        label: "Look"
        Choice { Layout.fillWidth: true; value: sec.cur; options: sec.looks.map(p => ({ v: p.name, label: p.name })); onChosen: (v) => s.cmd("cast_preset", { preset: v }) }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 12
        Toggle { text: "Physics (hair, straps)"; checked: !!sec.spec && sec.spec.physics !== false; onToggled: (v) => s.cmd("cast_physics", { value: v }) }
        Toggle { text: "Plaga / damage skin"; checked: !!sec.spec && !!sec.spec.gore; onToggled: (v) => s.cmd("cast_gore", { value: v }) }
    }
    RowLayout {
        Btn { text: "Duplicate"; tooltip: "A second copy next to this one, with the same look and copies of its tracks"; onClicked: s.cmd("duplicate_cast", {}) }
        Btn { kind: "danger"; icon: "trash-2"; text: "Remove from scene"; onClicked: s.cmd("destroy_object") }
    }
}
