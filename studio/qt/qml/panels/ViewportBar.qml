import QtQuick
import QtQuick.Layouts
import Director

// Below the picture, like SFM: manipulators on the left (Q W E R), transport in the middle, the viewport camera
// (Work / Scene / a camera) and picture tools on the right. Menus open as native popups so they float over the game.
Rectangle {
    id: root
    signal closeRequested()
    color: "#1c1c1f"
    Store { id: s }
    Rectangle { width: parent.width; height: 1; color: Theme.line }

    readonly property var gz: s.live ? s.st.gizmo : ({})
    readonly property bool driving: !!(s.st.drive && s.st.drive.on)
    readonly property bool recording: !!(s.st.drive && s.st.drive.recording)

    function tool(op) {
        if (op === "select") s.send("gizmo", { enabled: false })
        else s.send("gizmo", { enabled: true, tool: op })
    }
    function cameraMenu() {
        const items = [
            { header: "Look through" },
            { text: "Work Camera — free, never rendered", value: "work", checked: s.viewMode === "work" },
            { text: "Scene Camera — the shot's cuts", value: "scene", checked: s.viewMode === "scene" },
            { text: "Game Camera", value: "game", checked: s.viewMode === "game" },
        ]
        if (s.cameras.length) items.push({ separator: true })
        for (const c of s.cameras) items.push({ text: c.name, value: "cam:" + c.i, checked: c.live && s.viewMode !== "work" && s.viewMode !== "scene" })
        items.push({ separator: true })
        items.push({ text: "New camera from this view   (C)", value: "capture" })
        const r = studio.popup(items)
        if (r === "work") s.send("cam_work", { value: true })
        else if (r === "scene") s.send("cam_scene")
        else if (r === "game") s.send("cam_live", { i: 0 })
        else if (r === "capture") s.cmd("cam_capture")
        else if (typeof r === "string" && r.indexOf("cam:") === 0) s.send("cam_live", { i: parseInt(r.slice(4)) })
    }
    function overlayMenu() {
        const ov = s.live ? s.st.overlay : {}
        const r = studio.popup([
            { header: "Overlays (editor only)" },
            { text: "Skeleton", value: "skel", checked: !!(s.live && s.st.skeleton) },
            { text: "Onion skin (previous / next pose)", value: "onion", checked: !!(s.live && s.st.onion) },
            { text: "Rule of thirds", value: "thirds", checked: !!ov.thirds },
            { text: "Safe areas", value: "safe", checked: !!ov.safe },
            { text: "Centre cross", value: "center", checked: !!ov.center },
            { header: "Letterbox (part of the picture and renders)" },
            { text: "Off", value: "lb:0", checked: !ov.letterbox },
            { text: "1.85 : 1", value: "lb:1.85", checked: ov.letterbox === 1.85 },
            { text: "2 : 1", value: "lb:2", checked: ov.letterbox === 2 },
            { text: "2.39 : 1  scope", value: "lb:2.39", checked: ov.letterbox === 2.39 },
            { header: "Game" },
            { text: "Hide the game HUD", value: "hud", checked: !!(s.live && s.st.hud_hidden) },
            { text: "Hide Leon's weapon", value: "weap", checked: !!(s.live && s.st.hide_weapons) },
        ])
        if (r === "skel") s.send("overlay_skeleton", { value: !s.st.skeleton })
        else if (r === "onion") s.send("overlay_onion", { value: !s.st.onion })
        else if (r === "thirds" || r === "safe" || r === "center") { const f = {}; f[r] = !ov[r]; s.send("cam_overlay", { fields: f }) }
        else if (typeof r === "string" && r.indexOf("lb:") === 0) s.send("cam_overlay", { fields: { letterbox: parseFloat(r.slice(3)) } })
        else if (r === "hud") s.send("hud", { value: !!s.st.hud_hidden })
        else if (r === "weap") s.send("hide_weapons", { value: !s.st.hide_weapons })
    }
    function gizmoMenu() {
        const r = studio.popup([
            { header: "Manipulator works on" },
            { text: "The selected character or prop", value: "t:actor", checked: gz.target === "actor" },
            { text: "The selected camera", value: "t:camera", checked: gz.target === "camera" },
            { text: "A bone (Ctrl+click a bone dot)", value: "t:bone", checked: gz.target === "bone" },
            { header: "Snapping" },
            { text: "Off", value: "s:1", checked: gz.snap === 1 || !gz.snap },
            { text: "0.1 m · 15°", value: "s:2", checked: gz.snap === 2 },
            { text: "0.5 m · 45°", value: "s:3", checked: gz.snap === 3 },
            { text: "1 m · 90°", value: "s:4", checked: gz.snap === 4 },
        ])
        if (typeof r !== "string") return
        if (r.indexOf("t:") === 0) s.send("gizmo", { target: r.slice(2) })
        else s.send("gizmo", { snap: parseInt(r.slice(2)) })
    }
    function seekKey(dir) {
        // keys of the selected character (camera moves always count), else every key and clip edge
        const times = []
        for (const tr of s.seq.tracks || []) {
            if (s.actor && tr.actor !== s.actor.id && tr.kind !== "cammove") continue
            for (const k of tr.keys || []) times.push(k.t)
            for (const c of tr.clips || []) { times.push(c.start); for (const k of c.keys || []) times.push(c.start + k) }
            for (const c of tr.cuts || []) times.push(c.start)
        }
        times.sort((a, b) => a - b)
        const now = s.seq.t
        let target = -1
        if (dir > 0) { for (const x of times) if (x > now + 0.5) { target = x; break } }
        else { for (let i = times.length - 1; i >= 0; --i) if (times[i] < now - 0.5) { target = times[i]; break } }
        if (target >= 0) s.send("seq_seek", { t: target })
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6; anchors.rightMargin: 6
        spacing: 2
        IconButton { icon: "mouse-pointer-2"; tooltip: "Select (Q) — click things in the picture"; checked: !root.gz.enabled; onClicked: root.tool("select") }
        IconButton { icon: "move-3d"; tooltip: "Move (W)"; checked: !!root.gz.enabled && root.gz.op === "move"; onClicked: root.tool("move") }
        IconButton { icon: "rotate-3d"; tooltip: "Rotate (E)"; checked: !!root.gz.enabled && root.gz.op === "rotate"; onClicked: root.tool("rotate") }
        IconButton { icon: "scaling"; tooltip: "Scale (R)"; checked: !!root.gz.enabled && root.gz.op === "scale"; onClicked: root.tool("scale") }
        IconButton {
            icon: root.gz.mode === "local" ? "box" : "globe"
            text: root.gz.mode === "local" ? "Local" : "World"
            tooltip: "Manipulator axes (X)"
            onClicked: s.send("gizmo", { mode: root.gz.mode === "local" ? "world" : "local" })
        }
        IconButton {
            icon: root.gz.target === "bone" ? "bone" : root.gz.target === "camera" ? "video" : "user"
            tooltip: "What the manipulator moves, and snapping"
            onClicked: root.gizmoMenu()
        }
        Item { Layout.fillWidth: true }

        // transport
        IconButton { icon: "chevron-first"; tooltip: "Start (Home)"; onClicked: s.send("seq_seek", { t: s.range ? s.range[0] : 0 }) }
        IconButton { icon: "skip-back"; tooltip: "Previous key (↑)"; onClicked: root.seekKey(-1) }
        IconButton { icon: "step-back"; tooltip: "Previous frame (←, Shift: 10)"; onClicked: s.send("seq_seek", { t: Math.max(0, s.t - 1) }) }
        IconButton {
            icon: "circle-dot"; accent: true; checked: root.recording
            enabled: root.driving
            tooltip: root.driving ? (root.recording ? "Stop recording" : "Record the driven character (R in the game)") : "Record: drive a character first (Animation Set Editor ▸ Drive)"
            onClicked: s.send("record", { value: !root.recording })
        }
        IconButton {
            icon: s.seq.playing ? "pause" : "play"; size: 32; iconSize: 18; flat: false
            tooltip: "Play / pause (Space)"
            onClicked: s.send("seq_toggle")
        }
        IconButton { icon: "step-forward"; tooltip: "Next frame (→, Shift: 10)"; onClicked: s.send("seq_seek", { t: s.t + 1 }) }
        IconButton { icon: "skip-forward"; tooltip: "Next key (↓)"; onClicked: root.seekKey(1) }
        IconButton { icon: "chevron-last"; tooltip: "End (End)"; onClicked: s.send("seq_seek", { t: s.range ? s.range[1] : s.seq.length }) }
        IconButton { icon: "repeat"; tooltip: "Loop (L)"; checked: !!s.seq.loop; onClicked: s.send("seq_set", { loop: !s.seq.loop }) }
        Item { Layout.fillWidth: true }

        IconButton {
            icon: "video"
            text: s.viewLabel
            tooltip: "Viewport camera: Work Camera (free, never rendered) · Scene Camera (the shot's cuts) · any camera"
            flat: false
            Layout.maximumWidth: 240
            onClicked: root.cameraMenu()
        }
        IconButton { icon: "camera"; tooltip: "New camera from this view (C)"; onClicked: s.cmd("cam_capture") }
        IconButton {
            icon: "plane"; tooltip: "Fly the view: WASD · Q/E down/up · mouse look · Shift fast · F3 keys the camera · F1 back to the Studio"
            checked: !!(s.st.camera_fly && s.st.camera_fly.on)
            onClicked: { const on = !(s.st.camera_fly && s.st.camera_fly.on); s.send("cam_fly", { value: on }); if (on) studio.focusGame() }
        }
        IconButton {
            icon: "square-dashed-mouse-pointer"; tooltip: "Edit in the picture: click to select, drag to move, Ctrl+drag to turn, RMB look, MMB orbit, wheel dolly (F2 in the game)"
            checked: !!(s.st.edit && s.st.edit.on)
            onClicked: { const on = !(s.st.edit && s.st.edit.on); s.send("edit_mode", { value: on }); if (on) studio.focusGame() }
        }
        IconButton { icon: "layers"; tooltip: "Overlays, letterbox, HUD"; onClicked: root.overlayMenu() }
        IconButton {
            icon: "gamepad-2"; tooltip: "Mouse and keyboard to the game (F1 toggles from anywhere)"
            checked: studio.gameFocused
            onClicked: studio.toggleFocus()
        }
    }
}
