import QtQuick
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Constraints, solved after the animation every frame: hold something with one or both hands (IK pinned to
// another character's joint), plant a limb where it is, keep looking at something. Optional frame range.
Section {
    id: sec
    title: "Constraints"
    icon: "link"
    Store { id: s }
    readonly property var a: s.actor
    visible: !!a
    property string chain: "R_Hand"
    property int targetId: 0
    property string joint: ""
    readonly property var mine: a && s.live ? (s.st.constraints || []).filter(c => c.actor === a.id) : []
    readonly property var others: a ? s.actors.filter(o => o.id !== a.id) : []
    readonly property int target: targetId || (others.length ? others[0].id : 0)
    function add(kind, both) {
        s.cmd("constraint_add", { addr: a.id, kind: kind, chain: chain, both: !!both,
                                  target_actor: kind === "plant" ? undefined : (target || undefined), target_joint: joint || undefined })
    }

    Field {
        label: "Limb"
        Choice { Layout.fillWidth: true; value: sec.chain; options: [{ v: "R_Hand", label: "Right hand" }, { v: "L_Hand", label: "Left hand" }, { v: "R_Foot", label: "Right foot" }, { v: "L_Foot", label: "Left foot" }]; onChosen: (v) => sec.chain = v }
    }
    Field {
        label: "Target"
        Choice {
            Layout.fillWidth: true
            value: sec.target
            placeholder: "nothing else in the scene"
            options: sec.others.map(o => ({ v: o.id, label: N.actorLabel(o) }))
            onChosen: (v) => sec.targetId = v
        }
    }
    Field {
        label: "On joint"
        Choice {
            Layout.fillWidth: true
            value: sec.joint
            options: [{ v: "", label: "its centre" }].concat(["R_Wep", "L_Wep", "R_Hand", "L_Hand", "Head", "Spine_2", "Hip"].map(j => ({ v: j, label: j })))
            onChosen: (v) => sec.joint = v
        }
    }
    Flow {
        Layout.fillWidth: true
        spacing: 4
        Btn { kind: "primary"; text: "Hold with this hand"; enabled: sec.target > 0; onClicked: sec.add("ik_pin") }
        Btn { text: "Hold with both hands"; enabled: sec.target > 0; onClicked: sec.add("ik_pin", true) }
        Btn { text: "Plant this limb"; tooltip: "Freeze this limb where it is now, so it stays put while the body moves"; onClicked: sec.add("plant") }
        Btn { text: "Keep looking at it"; enabled: sec.target > 0; onClicked: sec.add("look") }
    }
    Text { text: "On " + N.actorLabel(sec.a) + " · " + sec.mine.length; color: Theme.textBright; font.pixelSize: Theme.fontSize; font.bold: true }
    Repeater {
        model: sec.mine
        Rectangle {
            id: card
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: col.implicitHeight + 10
            radius: 4
            color: Theme.bg2
            border.color: Theme.border
            ColumnLayout {
                id: col
                x: 6; y: 5
                width: parent.width - 12
                RowLayout {
                    Chip { text: card.modelData.kind === "plant" ? "Plant" : card.modelData.kind === "look" ? "Look" : "Hold"; checked: true }
                    Text {
                        Layout.fillWidth: true
                        text: card.modelData.chain + (card.modelData.target_name ? " → " + card.modelData.target_name + (card.modelData.target_joint ? " · " + card.modelData.target_joint : "") : "")
                        color: Theme.text; font.pixelSize: Theme.smallSize; elide: Text.ElideRight
                    }
                    Toggle { checked: card.modelData.enabled; onToggled: (v) => s.cmd("constraint_update", { id: card.modelData.id, fields: { enabled: v } }) }
                    IconButton { icon: "x"; size: 20; iconSize: 12; danger: true; onClicked: s.cmd("constraint_remove", { id: card.modelData.id }) }
                }
                SliderRow { channel: "constraint:" + card.modelData.id + "/weight"; label: "Strength"; step: 0.05; value: card.modelData.weight; onEdited: (v) => s.cmd("constraint_update", { id: card.modelData.id, fields: { weight: v } }) }
                Vec3Field { label: "Offset"; value: card.modelData.offset; onEdited: (v) => s.cmd("constraint_update", { id: card.modelData.id, fields: { offset: v } }) }
                RowLayout {
                    Text { text: card.modelData.range ? "frames " + card.modelData.range[0] + "–" + card.modelData.range[1] : "the whole film"; color: Theme.muted; font.pixelSize: Theme.smallSize }
                    Btn { kind: "ghost"; text: "Use the time selection"; onClicked: s.cmd("constraint_range", { id: card.modelData.id }) }
                    Btn { visible: !!card.modelData.range; kind: "ghost"; text: "Always"; onClicked: s.cmd("constraint_range", { id: card.modelData.id, clear: true }) }
                }
            }
        }
    }
}
