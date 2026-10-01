import QtQuick
import QtQuick.Layouts
import Director

// Ragdoll, cloth / hair simulation and the world's clock.
Section {
    id: sec
    title: "Physics"
    icon: "orbit"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a
    property real strength: 0.15

    Text {
        visible: !!sec.a && !sec.a.has_ragdoll
        Layout.fillWidth: true; wrapMode: Text.WordWrap
        text: "This one has no ragdoll - only characters built on the game's own bodies do (not spawned cast members)."
        color: Theme.muted; font.pixelSize: Theme.smallSize
    }
    Toggle { visible: !!sec.a && sec.a.has_ragdoll; text: "Go limp (physics takes over)"; checked: !!sec.a && !!sec.a.ragdoll; onToggled: (v) => s.cmd("ragdoll", { addr: sec.a.id, value: v, strength: sec.strength }) }
    SliderRow { visible: !!sec.a && sec.a.has_ragdoll; label: "Muscle"; value: sec.strength; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => { sec.strength = v; s.send("ragdoll_strength", { addr: sec.a.id, value: v }) } }
    Toggle { visible: !!sec.a && !!sec.a.cast; text: "Simulate hair, straps and cloth"; checked: !!sec.a && !!sec.a.cast && sec.a.cast.physics !== false; onToggled: (v) => s.cmd("cast_physics", { value: v }) }
    Toggle { text: "Freeze the world (time stands still)"; checked: s.live && !!s.st.game.frozen; onToggled: (v) => s.send("game_freeze", { value: v }) }
    Toggle { visible: !!sec.a && sec.a.has_fsm; text: "AI off (stops acting on its own)"; checked: !!sec.a && sec.a.puppet; onToggled: (v) => s.send("set_puppet", { value: v }) }
}
