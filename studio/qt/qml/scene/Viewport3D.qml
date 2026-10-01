import QtQuick
import QtQuick3D
import QtQuick3D.Helpers
import QtQuick.Layouts
import Director
import Director.Engine

// The Primary Viewport of the standalone runtime: the film rendered by Qt Quick 3D from the game's own assets.
// Work Camera navigation follows SFM: drag with the left button to look around (WASD / QE fly while it is held),
// middle button pans, right button dollies, Alt + left orbits the selection, the wheel dollies, F frames.
Item {
    id: root
    objectName: "viewport3d"
    focus: true
    readonly property var st: studio.state
    readonly property var overlay: st && st.overlay ? st.overlay : ({})
    readonly property bool ready: !!(st && st.game && st.game.ready)
    // the pen: clicks on the floor add path points or place a character ({ kind: path | place, actor, name })
    readonly property var pen: st && st.pen ? st.pen : null
    // ---- the manipulator: the runtime says where the handles sit (st.gizmo.frame), the picture draws them and turns a
    // drag into metres / degrees / a factor along one axis (gizmo_begin, gizmo_drag, gizmo_end)
    readonly property var gz: st && st.gizmo ? st.gizmo : ({})
    readonly property var gframe: gz.enabled !== false && gz.frame && gz.frame.pos && !(st.render_clock && st.render_clock.on) ? gz.frame : null
    readonly property string gop: gframe && gframe.kind === "bone" ? (gframe.ik && gz.op === "move" ? "move" : "rotate") : (gz.op || "move")
    // the selected character's posable bones (st.skeleton_joints): dots to click, lines to the parent
    readonly property var bones: st && st.skeleton_joints ? st.skeleton_joints : []
    function screenOf(p) { const v = view.mapFrom3DScene(vec(p)); return { x: v.x, y: v.y, z: v.z } }     // automation
    function boneAt(x, y) {
        let best = -1, bestD = 8
        for (let i = 0; i < bones.length; ++i) {
            const v = view.mapFrom3DScene(vec(bones[i].p))
            if (v.z <= 0) continue
            const d = Math.hypot(v.x - x, v.y - y)
            if (d < bestD) { bestD = d; best = i }
        }
        return best
    }
    property var gdrag: null
    property string lastPick: ""          // the map mesh last clicked (status bar, automation)
    property int ghover: -1
    function vec(a) { return Qt.vector3d(a[0], a[1], a[2]) }
    function gizmoSize() { return scene.ortho ? scene.orthoWidth * 0.07 : Math.max(0.04, cam.position.minus(vec(gframe.pos)).length() * 0.13) }
    // the handles in picture coordinates: one polyline per axis (rings for rotate), plus the centre for scale
    function handles() {
        if (!gframe) return []
        const o = vec(gframe.pos), L = gizmoSize(), so = view.mapFrom3DScene(o)
        if (so.z <= 0) return []
        const out = []
        for (let i = 0; i < 3; ++i) {
            const ax = vec(gframe.axes[i]).normalized()
            const pts = []
            if (gop === "rotate") {
                const u = (Math.abs(ax.y) < 0.9 ? Qt.vector3d(0, 1, 0) : Qt.vector3d(1, 0, 0)).crossProduct(ax).normalized(), w = ax.crossProduct(u)
                for (let k = 0; k <= 48; ++k) {
                    const a = k / 48 * Math.PI * 2
                    const q = view.mapFrom3DScene(o.plus(u.times(Math.cos(a) * L)).plus(w.times(Math.sin(a) * L)))
                    if (q.z > 0) pts.push(Qt.point(q.x, q.y))
                }
            } else {
                const e = view.mapFrom3DScene(o.plus(ax.times(L)))
                pts.push(Qt.point(so.x, so.y), Qt.point(e.x, e.y))
            }
            out.push({ axis: i, pts: pts })
        }
        if (gop === "scale") out.push({ axis: 3, pts: [Qt.point(so.x, so.y)] })
        return out
    }
    function hitGizmo(x, y) {
        let best = -1, bestD = 9
        for (const h of handles()) {
            if (h.pts.length === 1) { const d = Math.hypot(x - h.pts[0].x, y - h.pts[0].y); if (d < 11 && d < bestD) { bestD = d; best = h.axis }; continue }
            for (let k = 0; k + 1 < h.pts.length; ++k) {
                const a = h.pts[k], b = h.pts[k + 1], dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy
                const t = l2 > 0 ? Math.max(0, Math.min(1, ((x - a.x) * dx + (y - a.y) * dy) / l2)) : 0
                const d = Math.hypot(x - (a.x + t * dx), y - (a.y + t * dy))
                if (d < bestD) { bestD = d; best = h.axis }
            }
        }
        return best
    }
    function gizmoBegin(axis, x, y) {
        const o = vec(gframe.pos), L = gizmoSize(), ax = axis < 3 ? vec(gframe.axes[axis]).normalized() : Qt.vector3d(0, 1, 0)
        const s0 = view.mapFrom3DScene(o), s1 = view.mapFrom3DScene(o.plus(ax.times(L)))
        const toCam = cam.position.minus(o)
        gdrag = { axis: axis, x: x, y: y, s0: s0, s1: s1, L: L, sign: ax.dotProduct(toCam) > 0 ? -1 : 1,
                  last: Math.atan2(y - s0.y, x - s0.x) * 180 / Math.PI, turn: 0 }
        studio.send("gizmo_begin", { axis: axis, kind: gop })
    }
    function gizmoMove(x, y) {
        const d = gdrag
        let amount = 0
        if (gop === "rotate") {
            const a = Math.atan2(y - d.s0.y, x - d.s0.x) * 180 / Math.PI
            let step = a - d.last
            if (step > 180) step -= 360
            if (step < -180) step += 360
            d.last = a
            d.turn += step
            amount = d.turn * d.sign
        } else {
            const ex = d.s1.x - d.s0.x, ey = d.s1.y - d.s0.y, l2 = Math.max(1, ex * ex + ey * ey)
            const along = ((x - d.x) * ex + (y - d.y) * ey) / l2       // in handle lengths
            amount = gop === "scale" ? (d.axis === 3 ? 1 + ((x - d.x) - (y - d.y)) / 150 : 1 + along) : along * d.L
        }
        studio.send("gizmo_drag", { amount: amount })
    }

    // Drive: WASD walks the character (keys are captured like flying), R records, Esc stops
    readonly property var drive: st && st.drive && st.drive.on ? st.drive : null
    onDriveChanged: studio.setFlying(!!drive)
    Timer {
        interval: 16; repeat: true; running: !!root.drive
        property string sent: ""
        onTriggered: {
            const k = studio.flyKeys(), now = k.forward + "," + k.right + "," + k.fast
            if (now !== sent) { sent = now; scene.drive(k.forward, k.right, k.fast) }   // only changes: scripts can drive too
        }
        onRunningChanged: { sent = ""; if (!running) scene.drive(0, 0, false) }
    }
    // where a click in the picture meets the map or the floor ([x, y, z] or [])
    function groundAt(x, y) {
        const a = view.mapTo3DScene(Qt.vector3d(x, y, 0)), b = view.mapTo3DScene(Qt.vector3d(x, y, 10))
        return scene.groundHit(a, b.minus(a))
    }

    // the film's scene, shared by the viewport and the export view
    Node {
        id: sceneRoot
        // default studio lighting while the film has no lights of its own
        Node {
            DirectionalLight {
                visible: !!scene.world.sunOn
                eulerRotation: scene.world.sunEuler || Qt.vector3d(-38, 32, 0)
                brightness: scene.world.sunBrightness === undefined ? 1.9 : scene.world.sunBrightness
                color: scene.world.sunColor || "#fff4e6"
                castsShadow: true
                shadowMapQuality: Light.ShadowMapQualityVeryHigh
                shadowFactor: 65
                pcfFactor: 2
                csmNumSplits: 2
                shadowMapFar: 25
            }
            DirectionalLight { visible: scene.world.fills !== false; eulerRotation: Qt.vector3d(-12, -130, 0); brightness: 0.45 * (scene.world.sky === undefined ? 1 : scene.world.sky); color: "#c8d6ff" }
            DirectionalLight { visible: scene.world.fills !== false; eulerRotation: Qt.vector3d(-20, 175, 0); brightness: 0.7 * (scene.world.sky === undefined ? 1 : scene.world.sky); color: "#ffffff" }
        }
        Node { id: lightRoot }
        Node { id: stageRoot; objectName: "stageRoot" }
    }

    // Export view: the same scene at the output resolution, drawn only while a movie is being exported. It sits
    // under the viewport, so it is never seen; FilmRender sizes it and grabs its frames.
    View3D {
        id: renderView
        objectName: "renderView"
        property bool draft: false
        visible: false
        x: 0; y: 0; z: -1
        width: 1920; height: 1080
        importScene: sceneRoot
        camera: renderCam
        environment: ExtendedSceneEnvironment {
            id: renderEnv
            backgroundMode: SceneEnvironment.Color
            clearColor: "#2b2d31"
            antialiasingMode: renderView.draft ? SceneEnvironment.MSAA : SceneEnvironment.SSAA
            antialiasingQuality: renderView.draft ? SceneEnvironment.Medium : SceneEnvironment.High
            tonemapMode: SceneEnvironment.TonemapModeFilmic
            aoEnabled: true
            aoStrength: 45
            aoDistance: 0.35
            aoSoftness: 40
            lightProbe: Texture { source: studio.studioSky() }
            probeExposure: (scene.userLights ? 0.35 : 0.9) * (scene.world.sky === undefined ? 1 : scene.world.sky)
            specularAAEnabled: true
            ditheringEnabled: true
        }
        PerspectiveCamera {
            id: renderCam
            clipNear: 0.03
            clipFar: 3000
            fieldOfViewOrientation: PerspectiveCamera.Horizontal
        }
        Model {
            visible: !scene.hasStage && !!root.overlay.render_floor
            source: "#Rectangle"
            eulerRotation.x: -90
            scale: Qt.vector3d(10, 10, 1)
            materials: PrincipledMaterial { baseColor: "#55575b"; roughness: 0.95 }
        }
    }

    View3D {
        id: view
        anchors.fill: parent
        camera: scene.ortho ? orthoCam : cam
        importScene: sceneRoot
        renderMode: View3D.Offscreen
        environment: ExtendedSceneEnvironment {
            id: env
            backgroundMode: SceneEnvironment.Color
            clearColor: studio.shading === "rendered" ? "#303030" : Theme.viewport
            // Wireframe draws the edges (Solid is the materials' clay switch, see StageScene::applyClay)
            debugSettings: DebugSettings { wireframeEnabled: studio.shading === "wire" }
            antialiasingMode: SceneEnvironment.MSAA
            antialiasingQuality: SceneEnvironment.High
            temporalAAEnabled: true
            tonemapMode: SceneEnvironment.TonemapModeFilmic
            exposure: 1.0
            aoEnabled: true
            aoStrength: 45
            aoDistance: 0.35
            aoSoftness: 40
            lightProbe: Texture { source: studio.studioSky() }
            probeExposure: (scene.userLights ? 0.35 : 0.9) * (scene.world.sky === undefined ? 1 : scene.world.sky) * (studio.shading === "solid" ? 0.45 : 1)
            specularAAEnabled: true
            ditheringEnabled: true
        }

        PerspectiveCamera {
            id: cam
            clipNear: 0.03
            clipFar: 3000
            fieldOfView: 50
            fieldOfViewOrientation: PerspectiveCamera.Horizontal
        }
        // numpad 5: the same view without perspective, as wide as the perspective view is at the pivot
        OrthographicCamera {
            id: orthoCam
            position: cam.position
            rotation: cam.rotation
            clipNear: 0.01
            clipFar: 3000
            horizontalMagnification: Math.max(1, view.width) / Math.max(0.05, scene.orthoWidth)
            verticalMagnification: horizontalMagnification
        }

        // floor (viewport only): a soft grid that fades out with distance
        Model {
            visible: !scene.hasStage && !!(root.overlay.floor === undefined || root.overlay.floor)
            source: "#Rectangle"
            eulerRotation.x: -90
            scale: Qt.vector3d(10, 10, 1)
            receivesShadows: true
            castsShadows: false
            pickable: false
            materials: CustomMaterial {
                shadingMode: CustomMaterial.Shaded
                fragmentShader: "grid.frag"
                property color baseTint: "#3a3a3a"
                property color lineTint: "#565656"
            }
        }
    }

    StageScene {
        id: scene
        view: view
        root: stageRoot
        camera: cam
        environment: env
        lightRoot: lightRoot
        renderCamera: renderCam
        renderEnvironment: renderEnv
        Component.onCompleted: studio.attachScene(scene)
    }

    // ---- framing overlay (letterbox is part of the picture; guides only while editing)
    readonly property real lb: Number(overlay.letterbox || 0)
    readonly property real picH: lb > 0 ? Math.min(height, width / lb) : height
    Rectangle { visible: root.lb > 0; color: "black"; x: 0; y: 0; width: root.width; height: (root.height - root.picH) / 2 }
    Rectangle { visible: root.lb > 0; color: "black"; x: 0; y: root.height - (root.height - root.picH) / 2; width: root.width; height: (root.height - root.picH) / 2 }
    Item {
        id: guides
        x: 0; y: (root.height - root.picH) / 2; width: root.width; height: root.picH
        Repeater {
            model: root.overlay.thirds ? 2 : 0
            Rectangle { x: guides.width * (index + 1) / 3; width: 1; height: guides.height; color: "#66ffffff" }
        }
        Repeater {
            model: root.overlay.thirds ? 2 : 0
            Rectangle { y: guides.height * (index + 1) / 3; height: 1; width: guides.width; color: "#66ffffff" }
        }
        Rectangle {
            visible: !!root.overlay.safe
            anchors.centerIn: parent
            width: parent.width * 0.9; height: parent.height * 0.9
            color: "transparent"; border.color: "#55ffffff"; border.width: 1
        }
        Rectangle { visible: !!root.overlay.center; anchors.centerIn: parent; width: 24; height: 1; color: "#88ffffff" }
        Rectangle { visible: !!root.overlay.center; anchors.centerIn: parent; width: 1; height: 24; color: "#88ffffff" }
    }

    // ---- walk paths: the curve on the floor, its points, start and end (the selected character's brighter)
    Canvas {
        id: paths
        anchors.fill: view
        readonly property var tracks: root.st && root.st.sequence ? root.st.sequence.tracks.filter(t => t.kind === "path" && t.curve && t.curve.length > 1) : []
        visible: tracks.length > 0 && !(root.st.render_clock && root.st.render_clock.on)
        onTracksChanged: requestPaint()
        Connections { target: studio; function onStateChanged() { if (paths.visible) paths.requestPaint() } }
        Timer { interval: 33; repeat: true; running: paths.visible && mouse.pressed; onTriggered: paths.requestPaint() }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const sel = root.st && root.st.selection ? root.st.selection.actor : 0
            const at = (p) => { const v = view.mapFrom3DScene(Qt.vector3d(p[0], p[1] + 0.03, p[2])); return v.z > 0 ? v : null }
            for (const tr of tracks) {
                const mine = tr.actor === sel || !!tr.drawing
                ctx.strokeStyle = mine ? "#5fd9a6" : "rgba(95, 217, 166, 0.45)"
                ctx.fillStyle = ctx.strokeStyle
                ctx.lineWidth = mine ? 2.5 : 1.5
                ctx.beginPath()
                let open = false
                for (const p of tr.curve) {
                    const v = at(p)
                    if (!v) { open = false; continue }
                    if (open) ctx.lineTo(v.x, v.y); else { ctx.moveTo(v.x, v.y); open = true }
                }
                ctx.stroke()
                const pts = tr.pts || []
                for (let i = 0; i < pts.length; ++i) {
                    const v = at(pts[i])
                    if (!v) continue
                    ctx.beginPath()
                    ctx.arc(v.x, v.y, i === 0 ? 6 : 4, 0, Math.PI * 2)
                    ctx.fill()
                    if (mine && (i === 0 || i === pts.length - 1)) ctx.fillText(i === 0 ? "start" : "end", v.x + 8, v.y - 8)
                }
            }
        }
    }
    // the Studio closed without saving last time: offer the autosave back
    Rectangle {
        id: recoveryBanner
        readonly property var rec: root.st ? root.st.recovery : null
        visible: !!rec
        z: 20
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 24, recoveryRow.implicitWidth + 24)
        height: recoveryRow.implicitHeight + 16
        radius: 6
        color: "#2b2418"
        border.color: Theme.amber
        RowLayout {
            id: recoveryRow
            anchors.centerIn: parent
            spacing: 10
            Icon { name: "save"; size: 16; color: Theme.amber }
            Text {
                text: recoveryBanner.rec ? "RigReel Studio closed without saving. Unsaved work on \"" + recoveryBanner.rec.project + "\" from " + recoveryBanner.rec.time
                                           + " (" + recoveryBanner.rec.actors + " characters and props, " + recoveryBanner.rec.tracks + " tracks) was kept." : ""
                color: Theme.textBright
                font.pixelSize: Theme.smallSize
            }
            Btn { kind: "primary"; text: "Restore it"; onClicked: studio.cmd("recover_autosave", {}) }
            Btn { kind: "ghost"; text: "Discard"; onClicked: studio.send("discard_autosave", {}) }
        }
    }
    // the skeleton: posable bones of the selected character (Skeleton overlay, or while posing bones)
    Canvas {
        id: skeletonCanvas
        anchors.fill: view
        visible: root.bones.length > 0 && !(root.st.render_clock && root.st.render_clock.on)
        onVisibleChanged: requestPaint()
        Connections { target: studio; function onStateChanged() { if (skeletonCanvas.visible) skeletonCanvas.requestPaint() } }
        Timer { interval: 33; repeat: true; running: skeletonCanvas.visible && mouse.pressed; onTriggered: skeletonCanvas.requestPaint() }
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const sel = root.st && root.st.actors ? (root.st.actors.find(a => a.id === (root.st.selection || {}).actor) || {}) : {}
            const chosen = sel.pose ? sel.pose.joint : ""
            const pts = root.bones.map(b => view.mapFrom3DScene(root.vec(b.p)))
            ctx.lineWidth = 1.5
            ctx.strokeStyle = "rgba(120, 200, 255, 0.55)"
            for (let i = 0; i < root.bones.length; ++i) {
                const up = root.bones[i].up
                if (up < 0 || pts[i].z <= 0 || pts[up].z <= 0) continue
                ctx.beginPath(); ctx.moveTo(pts[up].x, pts[up].y); ctx.lineTo(pts[i].x, pts[i].y); ctx.stroke()
            }
            for (let i = 0; i < root.bones.length; ++i) {
                if (pts[i].z <= 0) continue
                const on = root.bones[i].n === chosen
                ctx.fillStyle = on ? "#ffd84a" : "rgba(170, 225, 255, 0.95)"
                ctx.beginPath(); ctx.arc(pts[i].x, pts[i].y, on ? 5 : 3.5, 0, Math.PI * 2); ctx.fill()
            }
        }
    }
    // the manipulator's handles
    Canvas {
        id: gizmoCanvas
        anchors.fill: view
        visible: !!root.gframe
        Connections { target: studio; function onStateChanged() { if (gizmoCanvas.visible) gizmoCanvas.requestPaint() } }
        Timer { interval: 33; repeat: true; running: gizmoCanvas.visible && mouse.pressed; onTriggered: gizmoCanvas.requestPaint() }
        onVisibleChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const colors = ["#e0524f", "#6cc95a", "#4f8ff0", "#f0f0f0"]
            const active = root.gdrag ? root.gdrag.axis : root.ghover
            for (const h of root.handles()) {
                ctx.strokeStyle = h.axis === active ? "#ffd84a" : colors[h.axis]
                ctx.fillStyle = ctx.strokeStyle
                ctx.lineWidth = h.axis === active ? 3.5 : 2.5
                if (h.pts.length === 1) { ctx.fillRect(h.pts[0].x - 6, h.pts[0].y - 6, 12, 12); continue }
                ctx.beginPath()
                ctx.moveTo(h.pts[0].x, h.pts[0].y)
                for (let k = 1; k < h.pts.length; ++k) ctx.lineTo(h.pts[k].x, h.pts[k].y)
                ctx.stroke()
                if (root.gop !== "rotate") {
                    const e = h.pts[h.pts.length - 1]
                    if (root.gop === "scale") ctx.fillRect(e.x - 5, e.y - 5, 10, 10)
                    else { ctx.beginPath(); ctx.arc(e.x, e.y, 5, 0, Math.PI * 2); ctx.fill() }
                }
            }
        }
    }
    // Drive banner
    Rectangle {
        visible: !!root.drive
        anchors { horizontalCenter: parent.horizontalCenter; bottom: parent.bottom; bottomMargin: 12 }
        width: driveText.implicitWidth + 28; height: 30; radius: 15
        color: root.drive && root.drive.recording ? "#cc4a1414" : "#cc1c2b40"
        border.color: root.drive && root.drive.recording ? "#ff5a5a" : "#6aa7ff"
        Text {
            id: driveText
            anchors.centerIn: parent
            color: "white"; font.pixelSize: 12
            text: !root.drive ? "" : (root.drive.recording ? "● REC  " : "") + "Driving " + (root.drive.name || "") + " · WASD walks, Shift jogs · R " + (root.drive.recording ? "stops recording" : "records") + " · Esc stops"
        }
    }
    // what a click does while the pen is out
    Rectangle {
        visible: !!root.pen
        anchors { horizontalCenter: parent.horizontalCenter; top: parent.top; topMargin: 10 }
        width: penText.implicitWidth + 24; height: 28; radius: 14
        color: "#cc1d3b2f"; border.color: "#5fd9a6"
        Text {
            id: penText
            anchors.centerIn: parent
            color: "#dff7ec"; font.pixelSize: 12
            text: !root.pen ? "" : root.pen.kind === "path" ? "Click the floor to add points to the walk path · Enter or right-click when done"
                                                           : "Click where " + (root.pen.name || "the character") + " should stand · right-click or Esc cancels"
        }
    }

    // ---- empty states
    Column {
        anchors.centerIn: parent
        spacing: 6
        visible: !root.ready || (st && st.actors && st.actors.length === 0 && !scene.hasStage && !(st.stage && st.stage.busy) && !(studio.data && studio.data.mesh_preview && studio.data.mesh_preview.name))
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.muted
            font.pixelSize: 15
            text: !root.ready ? (studio.games.opening ? "Opening " + studio.games.status.replace(/^Opening /, "") : "No game open")
                              : "An empty stage"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.faint
            font.pixelSize: 12
            text: !root.ready ? "Game ▸ Games… lists the games you own. Director reads their files; the game does not need to run."
                              : "Add characters and props from the Asset Browser."
        }
    }
    Text {
        anchors { right: parent.right; bottom: parent.bottom; margins: 8 }
        visible: !!(st && ((st.actors && st.actors.some(a => a.loading)) || (st.stage && st.stage.busy)))
        color: Theme.amber
        font.pixelSize: 12
        readonly property var mapStatus: st && st.stage && st.stage.busy ? st.stage.status : null
        text: mapStatus ? "Loading " + mapStatus.name + (mapStatus.total ? " · " + mapStatus.done + " / " + mapStatus.total + " meshes" : "…")
                        : "Loading from the game files…"
    }

    // ---- navigation
    MouseArea {
        id: mouse
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.MiddleButton | Qt.RightButton
        hoverEnabled: !!root.gframe
        cursorShape: root.pen ? Qt.CrossCursor : root.ghover >= 0 || root.gdrag ? Qt.PointingHandCursor : Qt.ArrowCursor
        property point last
        property point pressAt
        property int button: 0
        property bool moved: false
        property bool boxing: false
        readonly property bool blender: studio.navStyle !== "sfm"
        onPressed: (m) => {
            root.forceActiveFocus()
            // a handle under the mouse: drag it
            if (m.button === Qt.LeftButton && !(m.modifiers & Qt.AltModifier) && !root.pen && root.gframe) {
                const axis = root.hitGizmo(m.x, m.y)
                if (axis >= 0) { root.gizmoBegin(axis, m.x, m.y); last = Qt.point(m.x, m.y); button = m.button; moved = true; return }
            }
            last = Qt.point(m.x, m.y); pressAt = last; button = m.button; moved = false; boxing = false
            // looking around flies with WASD / QE while the button is held (right button in Blender style)
            studio.setFlying(blender ? m.button === Qt.RightButton : (m.button === Qt.LeftButton || m.button === Qt.RightButton))
            flyClock.restart()
        }
        onPositionChanged: (m) => {
            if (root.gdrag) { root.gizmoMove(m.x, m.y); return }
            if (!pressed) { const h = root.hitGizmo(m.x, m.y); if (h !== root.ghover) { root.ghover = h; gizmoCanvas.requestPaint() } return }
            const dx = m.x - last.x, dy = m.y - last.y
            last = Qt.point(m.x, m.y)
            if (Math.abs(m.x - pressAt.x) + Math.abs(m.y - pressAt.y) > 4) moved = true
            if (!moved) return
            if (blender) {
                if (button === Qt.MiddleButton) {
                    if (m.modifiers & Qt.ShiftModifier) scene.pan(dx, dy)
                    else if (m.modifiers & Qt.ControlModifier) scene.dolly(-dy * 0.05)
                    else scene.orbit(dx, dy)
                } else if (button === Qt.RightButton) scene.look(dx, dy)
                else if (button === Qt.LeftButton && (m.modifiers & Qt.AltModifier)) scene.orbit(dx, dy)
                else if (button === Qt.LeftButton && !root.pen) boxing = true
                return
            }
            if (button === Qt.LeftButton && (m.modifiers & Qt.AltModifier)) scene.orbit(dx, dy)
            else if (button === Qt.LeftButton) scene.look(dx, dy)
            else if (button === Qt.MiddleButton) scene.pan(dx, dy)
            else if (button === Qt.RightButton) scene.dolly(-dy * 0.05)
        }
        onReleased: (m) => {
            if (root.gdrag) { studio.send("gizmo_end", {}); root.gdrag = null; button = 0; gizmoCanvas.requestPaint(); return }
            studio.setFlying(!!root.drive)
            if (boxing) { root.boxSelect(pressAt, Qt.point(m.x, m.y), (m.modifiers & Qt.ShiftModifier) !== 0); boxing = false; button = 0; return }
            if (!moved && blender && !root.pen && m.button === Qt.RightButton) {
                const hit = view.pick(m.x, m.y)
                const id = hit.objectHit ? scene.actorOf(hit.objectHit) : 0
                if (id) scene.pickAt(hit.objectHit, false)
                root.contextMenu(id)
                button = 0
                return
            }
            if (!moved && root.pen && m.button === Qt.LeftButton) {
                const g = root.groundAt(m.x, m.y)
                if (g.length === 3) studio.cmd("pen_click", { pos: g })
            } else if (!moved && root.pen && m.button === Qt.RightButton) {
                studio.send(root.pen.kind === "path" ? "path_finish" : "pen_cancel", {})
            } else if (!moved && m.button === Qt.LeftButton && root.boneAt(m.x, m.y) >= 0) {
                // a bone dot: pose that bone (rotate; Move on a hand or foot drags the limb by IK)
                studio.send("select_joint", { name: root.bones[root.boneAt(m.x, m.y)].n })
                studio.send("gizmo", { target: "bone", enabled: true })
            } else if (!moved && m.button === Qt.LeftButton) {
                const hit = view.pick(m.x, m.y)
                scene.pickAt(hit.objectHit, (m.modifiers & Qt.ShiftModifier) !== 0)
                // a piece of the map: say what it is (the game's mesh name)
                // a piece of the map: say what it is (the game's mesh; the map is not pickable, a ray cast finds it)
                if (!hit.objectHit || !scene.actorOf(hit.objectHit)) {
                    const a = view.mapTo3DScene(Qt.vector3d(m.x, m.y, 0)), b = view.mapTo3DScene(Qt.vector3d(m.x, m.y, 10))
                    const n = scene.stageObjectAt(a, b.minus(a).normalized())
                    root.lastPick = n
                    if (n) studio.setStatus("Map: " + n.split("|")[0].split("/").pop() + "  (" + (n.split("|")[1] || "").split("/").pop() + ")", 8000)
                }
            }
            button = 0
        }
        onWheel: (w) => { scene.dolly(w.angleDelta.y > 0 ? 0.6 : -0.6); if (paths.visible) paths.requestPaint() }
    }
    Timer {
        id: flyClock
        interval: 16
        repeat: true
        running: mouse.pressed
        property double lastMs: Date.now()
        onRunningChanged: lastMs = Date.now()
        onTriggered: {
            const now = Date.now(), dt = Math.min(0.1, (now - lastMs) / 1000)
            lastMs = now
            const k = studio.flyKeys()
            if (!root.drive && (k.forward || k.right || k.up)) scene.fly(k.forward, k.right, k.up, dt, k.fast)
        }
    }
    // characters and props whose middle falls in the box become the selection (Shift adds)
    function boxSelect(a, b, add) {
        const x0 = Math.min(a.x, b.x), x1 = Math.max(a.x, b.x), y0 = Math.min(a.y, b.y), y1 = Math.max(a.y, b.y)
        const hits = (st && st.actors ? st.actors : []).filter(ac => {
            const v = view.mapFrom3DScene(scene.actorCenter(ac.id))
            return v.z > 0 && v.x >= x0 && v.x <= x1 && v.y >= y0 && v.y <= y1
        })
        if (!add) studio.send("select_clear", {})
        hits.forEach((ac, i) => studio.send("select_actor", { addr: ac.id, add: add || i > 0 }))
    }
    function contextMenu(id) {
        const a = id && st.actors ? st.actors.find(x => x.id === id) : null
        const items = a ? [
            { header: a.display_name || a.name },
            { text: "Frame it   (F)", value: "frame" },
            { text: "Duplicate", value: "dup" },
            { text: "Delete   (Del)", value: "del" },
            { separator: true },
            { text: "Move to the View", value: "tocam" },
            { text: "Face the View", value: "face" },
            { text: "Turn Around (180°)", value: "flip" },
            { text: "Hide", value: "hide" },
        ] : [
            { text: "Frame All", value: "all" },
            { text: "New Camera from View   (C)", value: "cam" },
            { text: "Add a Light Here", value: "light" },
            { separator: true },
            { text: "Front   (1)", value: "front" }, { text: "Right   (3)", value: "right" }, { text: "Top   (7)", value: "top" },
            { text: "Perspective / Orthographic   (5)", value: "ortho" },
        ]
        const r = studio.popup(items)
        if (!r) return
        if (r === "frame") studio.frameSelection()
        else if (r === "dup") studio.cmd("duplicate_cast", { addr: id })
        else if (r === "del") studio.cmd("destroy_object", { addr: id })
        else if (r === "tocam") studio.cmd("move_to_camera", { addr: id })
        else if (r === "face") studio.cmd("face_camera", { addr: id })
        else if (r === "flip") studio.cmd("flip_facing", { addr: id })
        else if (r === "hide") studio.send("actor_visible", { addr: id, value: false })
        else if (r === "cam") studio.cmd("cam_capture")
        else if (r === "light") studio.cmd("light_add", { kind: "point" })
        else studio.viewCommand(r)
    }
    Rectangle {
        visible: mouse.boxing
        x: Math.min(mouse.pressAt.x, mouse.last.x); y: Math.min(mouse.pressAt.y, mouse.last.y)
        width: Math.abs(mouse.last.x - mouse.pressAt.x); height: Math.abs(mouse.last.y - mouse.pressAt.y)
        color: Qt.rgba(1, 1, 1, 0.06)
        border.color: "#e0e0e0"; border.width: 1
    }
    ViewTools {
        visible: studio.standalone
        x: 8; y: 8
    }
    // the view's name and what is selected (Blender's "User Perspective / (1) Collection | Cube")
    Column {
        x: 50; y: 8
        spacing: 2
        readonly property var selActor: st && st.selection && st.actors ? st.actors.find(a => a.id === st.selection.actor) : null
        readonly property string viewMode: st ? (st.camera_view || (st.active_camera === 0 ? "work" : "camera")) : "work"
        Text {
            text: parent.viewMode === "work" ? studio.viewName + (studio.ortho ? " Orthographic" : " Perspective")
                                             : "Camera " + (studio.ortho ? "Orthographic" : "Perspective")
            color: "#e0e0e0"; font.pixelSize: 12
            style: Text.Outline; styleColor: "#40000000"
        }
        Text {
            text: "(" + Math.floor(st && st.sequence ? st.sequence.t : 0) + ") " + (st && st.game && st.game.project ? st.game.project : "untitled")
                  + (parent.selActor ? " | " + (parent.selActor.display_name || parent.selActor.name) : "")
            color: "#e0e0e0"; font.pixelSize: 12
            style: Text.Outline; styleColor: "#40000000"
        }
    }
    NavGizmo {
        visible: studio.standalone
        anchors.right: parent.right; anchors.top: parent.top
        anchors.rightMargin: 8; anchors.topMargin: 8
        camera: cam
        scene: scene
    }

    Keys.onPressed: (e) => {
        if (e.key === Qt.Key_F && !(e.modifiers & Qt.ControlModifier)) { scene.frameSelection(); e.accepted = true }
        else if (root.drive && e.key === Qt.Key_R && !e.isAutoRepeat) { studio.send("record", { value: !root.drive.recording }); e.accepted = true }
        else if (root.drive && e.key === Qt.Key_Escape) { studio.send("drive", { value: false }); e.accepted = true }
        else if (root.pen && (e.key === Qt.Key_Return || e.key === Qt.Key_Enter || e.key === Qt.Key_Escape)) {
            studio.send(root.pen.kind === "path" && e.key !== Qt.Key_Escape ? "path_finish" : "pen_cancel", {})
            e.accepted = true
        }
    }
}
