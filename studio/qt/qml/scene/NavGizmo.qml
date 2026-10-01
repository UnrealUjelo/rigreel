import QtQuick
import QtQuick.Controls.Basic
import Director

// Blender's navigation gizmo (top right of the view): the world's axes as the view sees them. Click an axis to
// look along it (again: from the other side), drag to orbit. Below it: drag to zoom, drag to pan, the camera view
// (Numpad 0) and perspective / orthographic (Numpad 5).
Item {
    id: root
    property var camera                  // the viewport's PerspectiveCamera
    property var scene                   // StageScene
    width: 96
    height: gizmo.height + 8 + pill.height

    readonly property var axes: [
        { v: Qt.vector3d(1, 0, 0), c: Theme.axisX, label: "X", view: "right" },
        { v: Qt.vector3d(0, 1, 0), c: Theme.axisY, label: "Y", view: "top" },
        { v: Qt.vector3d(0, 0, 1), c: Theme.axisZ, label: "Z", view: "front" },
        { v: Qt.vector3d(-1, 0, 0), c: Theme.axisX, label: "", view: "left" },
        { v: Qt.vector3d(0, -1, 0), c: Theme.axisY, label: "", view: "bottom" },
        { v: Qt.vector3d(0, 0, -1), c: Theme.axisZ, label: "", view: "back" },
    ]
    property int hover: -1
    function rgba(c, a, mix) {
        // mix: share of the axis colour over the viewport grey (#3f3f3f)
        const k = mix === undefined ? 1 : mix, g = 0.247 * (1 - k)
        return "rgba(" + Math.round((c.r * k + g) * 255) + "," + Math.round((c.g * k + g) * 255) + "," + Math.round((c.b * k + g) * 255) + "," + a + ")"
    }
    function points() {
        const inv = root.camera ? root.camera.rotation.inverted() : Qt.quaternion(1, 0, 0, 0)
        const cx = gizmo.width / 2, cy = gizmo.height / 2, r = 32
        return root.axes.map((a, i) => { const p = inv.times(a.v); return { i: i, x: cx + p.x * r, y: cy - p.y * r, z: p.z, a: a } })
    }
    function hit(x, y) {
        let best = -1, bd = 11
        for (const p of points()) { const d = Math.hypot(p.x - x, p.y - y); if (d < bd) { bd = d; best = p.i } }
        return best
    }

    Item {
        id: gizmo
        width: 96; height: 96
        Rectangle { anchors.fill: parent; radius: width / 2; color: "#ffffff"; opacity: gm.containsMouse || gm.pressed ? 0.09 : 0 }
        Canvas {
            id: cv
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const pts = root.points().sort((p, q) => p.z - q.z)
                const cx = width / 2, cy = height / 2
                for (const p of pts) {
                    const pos = p.a.label !== "", hov = root.hover === p.i
                    if (pos) {
                        ctx.strokeStyle = root.rgba(p.a.c, 1)
                        ctx.lineWidth = 2
                        ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(p.x, p.y); ctx.stroke()
                    }
                    ctx.beginPath()
                    ctx.arc(p.x, p.y, pos ? 9 : 8, 0, Math.PI * 2)
                    if (pos) {
                        ctx.fillStyle = hov ? "rgba(255,255,255,1)" : root.rgba(p.a.c, 1)
                        ctx.fill()
                        ctx.fillStyle = hov ? root.rgba(p.a.c, 1) : "rgba(0,0,0,0.8)"
                        ctx.font = "bold 11px sans-serif"
                        ctx.textAlign = "center"
                        ctx.textBaseline = "middle"
                        ctx.fillText(p.a.label, p.x, p.y + 0.5)
                    } else {
                        ctx.fillStyle = hov ? root.rgba(p.a.c, 1, 0.75) : root.rgba(p.a.c, 1, 0.3)
                        ctx.fill()
                        ctx.strokeStyle = root.rgba(p.a.c, 1)
                        ctx.lineWidth = 1.4
                        ctx.stroke()
                    }
                }
            }
        }
        Connections { target: root.camera; function onRotationChanged() { cv.requestPaint() } }
        MouseArea {
            id: gm
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: pressed ? Qt.ClosedHandCursor : root.hover >= 0 ? Qt.PointingHandCursor : Qt.OpenHandCursor
            property point last
            property bool moved: false
            onPositionChanged: (m) => {
                if (pressed) {
                    const dx = m.x - last.x, dy = m.y - last.y
                    if (Math.abs(dx) + Math.abs(dy) > 0) moved = true
                    last = Qt.point(m.x, m.y)
                    if (root.scene) root.scene.orbit(dx * 1.6, dy * 1.6)
                } else {
                    const h = root.hit(m.x, m.y)
                    if (h !== root.hover) { root.hover = h; cv.requestPaint() }
                }
            }
            onExited: { root.hover = -1; cv.requestPaint() }
            onPressed: (m) => { last = Qt.point(m.x, m.y); moved = false }
            onReleased: (m) => {
                if (moved) return
                const h = root.hit(m.x, m.y)
                if (h < 0) return
                const a = root.axes[h]
                const name = a.view.charAt(0).toUpperCase() + a.view.slice(1)
                studio.viewCommand(studio.viewName === name ? "flip" : a.view)
            }
        }
    }

    // zoom, pan, camera, orthographic
    Rectangle {
        id: pill
        y: gizmo.height + 8
        anchors.horizontalCenter: gizmo.horizontalCenter
        width: 30
        height: col.implicitHeight + 8
        radius: 15
        color: Qt.rgba(0.216, 0.216, 0.216, 0.85)
        Column {
            id: col
            y: 4
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 2
            Repeater {
                model: [
                    { icon: "zoom-in", tip: "Drag to zoom (wheel)", kind: "zoom" },
                    { icon: "hand", tip: "Drag to pan (Shift + middle drag)", kind: "pan" },
                    { icon: "video", tip: "Look through the film's camera and back (Numpad 0)", kind: "camera" },
                    { icon: "grid-3x3", tip: "Perspective / Orthographic (Numpad 5)", kind: "ortho" },
                ]
                Rectangle {
                    required property var modelData
                    width: 26; height: 26; radius: 13
                    readonly property bool on: modelData.kind === "ortho" ? studio.ortho : false
                    color: on ? Theme.accent : pm.containsMouse || pm.pressed ? "#555555" : "transparent"
                    Icon { anchors.centerIn: parent; name: modelData.icon; size: 15; color: "#d0d0d0" }
                    MouseArea {
                        id: pm
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: modelData.kind === "zoom" ? Qt.SizeVerCursor : modelData.kind === "pan" ? Qt.SizeAllCursor : Qt.PointingHandCursor
                        property point last
                        onPressed: (m) => last = Qt.point(m.x, m.y)
                        onPositionChanged: (m) => {
                            if (!pressed || !root.scene) return
                            const dx = m.x - last.x, dy = m.y - last.y
                            last = Qt.point(m.x, m.y)
                            if (modelData.kind === "zoom") root.scene.dolly(-dy * 0.08)
                            else if (modelData.kind === "pan") root.scene.pan(-dx * 2, -dy * 2)
                        }
                        onClicked: if (modelData.kind === "camera" || modelData.kind === "ortho") studio.viewCommand(modelData.kind)
                    }
                    ToolTip.visible: pm.containsMouse && !pm.pressed
                    ToolTip.text: modelData.tip
                    ToolTip.delay: 500
                }
            }
        }
    }
}
