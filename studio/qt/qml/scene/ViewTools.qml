import QtQuick
import QtQuick.Controls.Basic
import Director

// The toolbar on the left of the view, like Blender's: select, move, rotate, scale; posing; a walk path; a camera
// from the view. The active tool is blue.
Column {
    id: root
    spacing: 6
    property var st: studio.state
    readonly property var gz: st && st.gizmo ? st.gizmo : ({})
    readonly property bool pose: gz.target === "bone"
    readonly property var actor: st && st.selection && st.actors ? (st.actors.find(a => a.id === st.selection.actor) || null) : null

    component Tool: Rectangle {
        property string icon: ""
        property string tip: ""
        property bool active: false
        property bool first: false
        property bool last: false
        signal clicked()
        width: 32; height: 32
        radius: first || last ? 5 : 0
        color: active ? Theme.accent : tm.containsMouse ? "#3c3c3c" : "#282828"
        Rectangle { visible: !parent.first; width: parent.width; height: parent.radius; color: parent.color }
        Rectangle { visible: !parent.last; anchors.bottom: parent.bottom; width: parent.width; height: parent.radius; color: parent.color }
        Icon { anchors.centerIn: parent; name: parent.icon; size: 17; color: parent.active ? "#ffffff" : tm.containsMouse ? "#e5e5e5" : "#bababa" }
        MouseArea { id: tm; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
        ToolTip.visible: tm.containsMouse
        ToolTip.text: tip
        ToolTip.delay: 400
    }
    function tool(op) {
        if (op === "select") studio.send("gizmo", { enabled: false })
        else studio.send("gizmo", { enabled: true, tool: op })
    }

    Column {
        Tool { first: true; icon: "box-select"; tip: "Select (Q): click, or drag a box; Shift adds"; active: root.gz.enabled === false; onClicked: root.tool("select") }
        Tool { icon: "move-3d"; tip: root.pose ? "Move (W): drag a hand or foot, the arm or leg follows (IK)" : "Move (W)"; active: root.gz.enabled !== false && root.gz.op === "move"; onClicked: root.tool("move") }
        Tool { icon: "rotate-3d"; tip: "Rotate (E)"; active: root.gz.enabled !== false && root.gz.op === "rotate"; onClicked: root.tool("rotate") }
        Tool { last: true; icon: "scaling"; tip: "Scale (R)"; active: root.gz.enabled !== false && root.gz.op === "scale"; onClicked: root.tool("scale") }
    }
    Column {
        Tool {
            first: true; icon: "bone"; active: root.pose
            tip: root.pose ? "Pose Mode: click a bone dot, turn it with the rings (Ctrl+Tab back to Object Mode)" : "Pose Mode: turn a character's bones (Ctrl+Tab)"
            onClicked: {
                if (root.pose) { studio.send("gizmo", { target: "actor" }); studio.send("overlay_skeleton", { value: false }) }
                else { studio.send("gizmo", { target: "bone", enabled: true }); studio.send("overlay_skeleton", { value: true }) }
            }
        }
        Tool {
            last: true; icon: "route"; active: !!(root.st && root.st.pen && root.st.pen.kind === "path")
            tip: root.actor && root.actor.kind !== "object" ? "Walk path: click points on the floor for " + (root.actor.display_name || root.actor.name) + " to walk (Enter ends)" : "Walk path: select a character first"
            opacity: root.actor && root.actor.kind !== "object" ? 1 : 0.45
            onClicked: if (root.actor && root.actor.kind !== "object") studio.cmd("path_new", { addr: root.actor.id })
        }
    }
    Column {
        Tool { first: true; last: true; icon: "camera"; tip: "New camera from the view (C)"; onClicked: studio.cmd("cam_capture") }
    }
}
