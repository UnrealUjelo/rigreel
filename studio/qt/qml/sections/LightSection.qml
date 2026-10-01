import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// A Director light: colour (or temperature), intensity, range, cone, shadows and the film controls
// (bounce, fog, highlights, shadow softness / bias).
Section {
    id: sec
    Store { id: s }
    readonly property var l: s.light
    visible: !!l
    title: l ? "Light · " + l.name : "Light"
    icon: "lightbulb"
    function upd(f) { s.cmd("light_update", { id: l.id, fields: f }) }
    function ch(f) { return l ? "light:" + l.id + "/" + f : "" }

    Field { label: "Name"; TextBox { Layout.fillWidth: true; text: sec.l ? sec.l.name : ""; onCommitted: (t) => sec.upd({ name: t }) } }
    RowLayout {
        spacing: 14
        Toggle { text: "On"; checked: !!sec.l && sec.l.enabled; onToggled: (v) => sec.upd({ enabled: v }) }
        KeyButton { channel: sec.ch("enabled"); Layout.leftMargin: -10 }
        Toggle { text: "Shadows"; checked: !!sec.l && sec.l.shadows; onToggled: (v) => sec.upd({ shadows: v }) }
        Chip { text: sec.l && sec.l.kind === "spot" ? "Spot" : "Point"; checked: true }
    }
    SliderRow { channel: sec.ch("intensity"); label: "Intensity"; from: 0; to: 20000; step: 10; value: sec.l ? sec.l.intensity : 0; fill: "#8a6a2a"; format: (v) => v.toFixed(0); onEdited: (v) => sec.upd({ intensity: v }) }
    Toggle { text: "Colour from temperature"; checked: !!sec.l && !!sec.l.blackbody; onToggled: (v) => sec.upd({ blackbody: v }) }
    SliderRow { channel: sec.ch("temperature"); visible: !!sec.l && !!sec.l.blackbody; label: "Temperature"; from: 1500; to: 12000; step: 50; value: sec.l ? sec.l.temperature || 6500 : 6500; format: (v) => v.toFixed(0) + " K"; onEdited: (v) => sec.upd({ temperature: v }) }
    Field {
        visible: !!sec.l && !sec.l.blackbody
        label: "Colour"
        // wraps in a narrow panel (a fixed row made the whole page wider than the dock)
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Rectangle { width: 22; height: 20; radius: 3; color: sec.l ? N.hexOf(sec.l.color) : "white"; border.color: Theme.border }
            Repeater {
                model: [["#fff4e0", "warm"], ["#ffffff", "white"], ["#cfe4ff", "cool"], ["#ff9a4a", "fire"], ["#7fb4ff", "moon"], ["#ff4040", "red"], ["#40ff80", "green"]]
                Rectangle {
                    required property var modelData
                    width: 20; height: 20; radius: 10; color: modelData[0]; border.color: Theme.border
                    MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor; onClicked: sec.upd({ color: N.rgbOf(parent.modelData[0]) }) }
                }
            }
        }
    }
    SliderRow { channel: sec.ch("radius"); label: "Range"; from: 0.5; to: 40; step: 0.1; value: sec.l ? sec.l.radius : 5; format: (v) => v.toFixed(1) + " m"; onEdited: (v) => sec.upd({ radius: v }) }
    SliderRow { channel: sec.ch("cone"); visible: !!sec.l && sec.l.kind === "spot"; label: "Cone"; from: 2; to: 120; step: 0.5; value: sec.l ? sec.l.cone : 45; format: (v) => v.toFixed(0) + "°"; onEdited: (v) => sec.upd({ cone: v }) }
    SliderRow { channel: sec.ch("spread"); visible: !!sec.l && sec.l.kind === "spot"; label: "Edge softness"; from: 0; to: 1; value: sec.l ? sec.l.spread : 0.5; onEdited: (v) => sec.upd({ spread: v }) }
    SliderRow { channel: sec.ch("bounce"); label: "Bounce"; from: 0; to: 4; step: 0.05; value: sec.l ? (sec.l.bounce === undefined ? 1 : sec.l.bounce) : 1; onEdited: (v) => sec.upd({ bounce: v }) }
    SliderRow { channel: sec.ch("volumetric"); label: "In the air (fog)"; from: 0; to: 4; step: 0.05; value: sec.l ? (sec.l.volumetric === undefined ? 1 : sec.l.volumetric) : 1; onEdited: (v) => sec.upd({ volumetric: v }) }
    SliderRow { channel: sec.ch("specular"); label: "Highlights"; from: 0; to: 4; step: 0.05; value: sec.l ? (sec.l.specular === undefined ? 1 : sec.l.specular) : 1; onEdited: (v) => sec.upd({ specular: v }) }
    SliderRow { label: "Shadow softness"; from: 0; to: 10; step: 0.1; value: sec.l ? sec.l.shadow_soft || 0 : 0; onEdited: (v) => sec.upd({ shadow_soft: v }) }
    SliderRow { label: "Shadow bias"; from: 0; to: 0.2; step: 0.001; value: sec.l ? sec.l.shadow_bias || 0 : 0; format: (v) => v.toFixed(3); onEdited: (v) => sec.upd({ shadow_bias: v }) }
    Vec3Field { label: "Position"; value: sec.l ? sec.l.pos : [0, 0, 0]; onEdited: (v) => sec.upd({ pos: v }) }
    Vec3Field { label: "Rotation"; step: 1; value: sec.l ? sec.l.euler : [0, 0, 0]; onEdited: (v) => sec.upd({ euler: v }) }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Btn { text: "Aim at " + (s.actor ? N.actorLabel(s.actor) : "selection"); enabled: !!s.actor; onClicked: s.cmd("light_aim", { id: sec.l.id }) }
        Btn { text: "From view"; tooltip: "Put the light where the view is, pointing the same way"; onClicked: s.cmd("light_to_camera", { id: sec.l.id }) }
        Btn { kind: "danger"; icon: "trash-2"; text: "Remove"; onClicked: { s.cmd("light_remove", { id: sec.l.id }); s.send("light_select", {}) } }
    }
}
