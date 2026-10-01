import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director

// The 3D view's header, after Blender's: the mode and the View / Select / Add / Object menus on the left,
// orientation and snapping in the middle, the camera, overlays and the four shadings on the right. Menus open as
// native popups (they float over the game in live mode).
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg2
    Store { id: s }

    readonly property var st: s.st
    readonly property var gz: s.live && st.gizmo ? st.gizmo : ({})
    readonly property bool pose: gz.target === "bone"
    readonly property var sel: s.actor

    // ---- menus
    function modeMenu() {
        const r = studio.popup([
            { header: "Interaction mode" },
            { text: "Object Mode — move, turn and scale characters, props, cameras", value: "object", checked: !root.pose && root.gz.target !== "camera" },
            { text: "Pose Mode — turn bones, drag hands and feet (IK)", value: "pose", checked: root.pose },
            { text: "Camera Mode — the manipulator moves the selected camera", value: "camera", checked: root.gz.target === "camera" },
        ])
        root.setMode(r)
    }
    function setMode(m) {
        if (m === "pose") { s.send("gizmo", { target: "bone", enabled: true }); s.send("overlay_skeleton", { value: true }) }
        else if (m === "object") { s.send("gizmo", { target: "actor" }); s.send("overlay_skeleton", { value: false }) }
        else if (m === "camera") s.send("gizmo", { target: "camera", enabled: true })
    }
    function viewMenu() {
        const r = studio.popup([
            { text: "Frame Selected   (F · Numpad .)", value: "selected" },
            { text: "Frame All   (Numpad Home)", value: "all" },
            { separator: true },
            { text: "Viewpoint", items: [
                { text: "Front   (1)", value: "front" }, { text: "Back   (Ctrl+1)", value: "back" },
                { text: "Right   (3)", value: "right" }, { text: "Left   (Ctrl+3)", value: "left" },
                { text: "Top   (7)", value: "top" }, { text: "Bottom   (Ctrl+7)", value: "bottom" },
                { separator: true }, { text: "Opposite side   (9)", value: "flip" },
            ] },
            { text: "Perspective / Orthographic   (5)", value: "ortho", checked: studio.ortho },
            { text: "Camera   (0)", value: "camera" },
            { separator: true },
            { text: "Look through", items: [
                { text: "Work Camera — free, never rendered", value: "work", checked: s.viewMode === "work" },
                { text: "Scene Camera — the film's cuts", value: "scene", checked: s.viewMode === "scene" },
            ].concat(s.cameras.map(c => ({ text: c.name, value: "cam:" + c.i, checked: c.live && s.viewMode !== "work" && s.viewMode !== "scene" }))) },
            { text: "New Camera from View   (C)", value: "capture" },
            { separator: true },
            { text: "Navigation", items: [
                { text: "Blender — middle drag orbits, Shift pans, Ctrl zooms, right drag looks (WASD flies)", value: "nav:blender", checked: studio.navStyle === "blender" },
                { text: "Source Filmmaker — left drag looks, middle pans, right dollies, Alt orbits", value: "nav:sfm", checked: studio.navStyle === "sfm" },
            ] },
        ])
        if (typeof r !== "string") return
        if (r === "work") s.send("cam_work", { value: true })
        else if (r === "scene") s.send("cam_scene")
        else if (r === "capture") s.cmd("cam_capture")
        else if (r.indexOf("cam:") === 0) s.send("cam_live", { i: parseInt(r.slice(4)) })
        else if (r.indexOf("nav:") === 0) studio.navStyle = r.slice(4)
        else studio.viewCommand(r)
    }
    function selectMenu() {
        const r = studio.popup([
            { text: "All", value: "all" },
            { text: "None   (Esc)", value: "none" },
            { text: "Invert", value: "invert" },
            { separator: true },
            { text: "All characters", value: "chars" },
            { text: "All props", value: "props" },
        ])
        const actors = s.actors || []
        const cur = new Set([s.sel.actor].concat(s.sel.multi || []).filter(x => !!x))
        let pick = null
        if (r === "all") pick = actors
        else if (r === "chars") pick = actors.filter(a => a.kind !== "object")
        else if (r === "props") pick = actors.filter(a => a.kind === "object")
        else if (r === "invert") pick = actors.filter(a => !cur.has(a.id))
        else if (r === "none") { s.send("select_clear"); return }
        if (!pick) return
        s.send("select_clear")
        pick.forEach((a, i) => s.send("select_actor", { addr: a.id, add: i > 0 }))
    }
    function addMenu() {
        const r = studio.popup([
            { text: "Character…   (Asset Browser)", value: "char" },
            { text: "Prop…   (Asset Browser)", value: "prop" },
            { text: "Map…   (Asset Browser)", value: "map" },
            { separator: true },
            { text: "Camera from View   (C)", value: "cam" },
            { text: "Light", items: [
                { text: "Point", value: "light:point" }, { text: "Spot", value: "light:spot" }, { text: "Sun", value: "light:sun" },
            ] },
            { separator: true },
            { text: "Walk Path for the selected character", value: "path", enabled: !!root.sel && root.sel.kind !== "object" },
        ])
        if (r === "char" || r === "prop" || r === "map") { Ui.assetTab = r === "char" ? "characters" : r === "prop" ? "props" : "stages"; Ui.focusAssets(Ui.assetTab) }
        else if (r === "cam") s.cmd("cam_capture")
        else if (typeof r === "string" && r.indexOf("light:") === 0) s.cmd("light_add", { kind: r.slice(6) })
        else if (r === "path" && root.sel) { s.cmd("path_new", { addr: root.sel.id }); studio.focusGame() }
    }
    function objectMenu() {
        const a = root.sel
        const r = studio.popup([
            { text: "Duplicate", value: "dup", enabled: !!a },
            { text: "Delete   (Del)", value: "del", enabled: !!a },
            { separator: true },
            { text: "Transform", items: [
                { text: "Move to the View", value: "tocam", enabled: !!a },
                { text: "Face the View", value: "face", enabled: !!a },
                { text: "Turn Around (180°)", value: "flip", enabled: !!a },
            ] },
            { text: "Play clips in place (lock root motion)", value: "lock", enabled: !!a, checked: !!(a && a.root_lock) },
            { text: "Hide", value: "hide", enabled: !!a, checked: !!(a && a.hidden) },
            { separator: true },
            { text: "Frame it   (F)", value: "frame", enabled: !!a },
        ])
        if (!a) return
        if (r === "dup") s.cmd("duplicate_cast", { addr: a.id })
        else if (r === "del") s.cmd("destroy_object", { addr: a.id })
        else if (r === "tocam") s.cmd("move_to_camera", { addr: a.id })
        else if (r === "face") s.cmd("face_camera", { addr: a.id })
        else if (r === "flip") s.cmd("flip_facing", { addr: a.id })
        else if (r === "lock") s.cmd("set_root_lock", { addr: a.id, value: !a.root_lock })
        else if (r === "hide") s.send("actor_visible", { addr: a.id, value: !!a.hidden })
        else if (r === "frame") studio.frameSelection()
    }
    function overlayMenu() {
        const ov = s.live ? st.overlay : {}
        const r = studio.popup([
            { header: "Viewport overlays" },
            { text: "Skeleton   (Shift+B)", value: "skel", checked: !!(s.live && st.skeleton) },
            { text: "Onion skin (previous / next pose)", value: "onion", checked: !!(s.live && st.onion) },
            { text: "Rule of thirds", value: "thirds", checked: !!ov.thirds },
            { text: "Safe areas", value: "safe", checked: !!ov.safe },
            { text: "Centre cross", value: "center", checked: !!ov.center },
            { header: "Letterbox (part of the picture and renders)" },
            { text: "Off", value: "lb:0", checked: !ov.letterbox },
            { text: "1.85 : 1", value: "lb:1.85", checked: ov.letterbox === 1.85 },
            { text: "2 : 1", value: "lb:2", checked: ov.letterbox === 2 },
            { text: "2.39 : 1  scope", value: "lb:2.39", checked: ov.letterbox === 2.39 },
        ])
        if (r === "skel") s.send("overlay_skeleton", { value: !st.skeleton })
        else if (r === "onion") s.send("overlay_onion", { value: !st.onion })
        else if (r === "thirds" || r === "safe" || r === "center") { const f = {}; f[r] = !ov[r]; s.send("cam_overlay", { fields: f }) }
        else if (typeof r === "string" && r.indexOf("lb:") === 0) s.send("cam_overlay", { fields: { letterbox: parseFloat(r.slice(3)) } })
    }
    function cameraMenu() {
        const items = [
            { header: "Look through" },
            { text: "Work Camera — free, never rendered", value: "work", checked: s.viewMode === "work" },
            { text: "Scene Camera — the film's cuts", value: "scene", checked: s.viewMode === "scene" },
        ]
        if (!studio.standalone) items.push({ text: "Game Camera", value: "game", checked: s.viewMode === "game" })
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
    function snapMenu() {
        const r = studio.popup([
            { header: "Snap increments" },
            { text: "Off", value: 1, checked: root.gz.snap === 1 || !root.gz.snap },
            { text: "0.1 m · 15°", value: 2, checked: root.gz.snap === 2 },
            { text: "0.5 m · 45°", value: 3, checked: root.gz.snap === 3 },
            { text: "1 m · 90°", value: 4, checked: root.gz.snap === 4 },
        ])
        if (r) s.send("gizmo", { snap: r })
    }

    // a header menu word (View, Select ...)
    component MenuWord: Rectangle {
        property string text: ""
        signal clicked()
        implicitWidth: label.implicitWidth + 16
        implicitHeight: 22
        radius: Theme.radius
        color: mw.containsMouse ? "#454545" : "transparent"
        Text { id: label; anchors.centerIn: parent; text: parent.text; color: Theme.text; font.pixelSize: Theme.fontSize }
        MouseArea { id: mw; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
    }
    // a dark dropdown button (mode, orientation, camera)
    component Drop: Rectangle {
        property string icon: ""
        property string text: ""
        property string tooltip: ""
        property color edge: "#3d3d3d"
        signal clicked()
        implicitWidth: Math.min(210, dr.implicitWidth + 16)
        implicitHeight: 22
        radius: Theme.radius
        color: dm.containsMouse ? "#2f2f2f" : "#282828"
        border.color: edge
        Row {
            id: dr
            anchors.centerIn: parent
            spacing: 6
            Icon { name: parent.parent.icon; size: 13; color: Theme.text; anchors.verticalCenter: parent.verticalCenter }
            Text { text: parent.parent.text; color: Theme.text; font.pixelSize: Theme.fontSize; elide: Text.ElideRight; width: Math.min(implicitWidth, 150); anchors.verticalCenter: parent.verticalCenter }
            Icon { name: "chevron-down"; size: 11; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
        }
        MouseArea { id: dm; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
        ToolTip.visible: tooltip !== "" && dm.containsMouse
        ToolTip.text: tooltip
        ToolTip.delay: 500
    }
    // buttons that belong together sit in one rounded pill (Blender)
    component Seg: Rectangle {
        property string icon: ""
        property string tooltip: ""
        property bool checked: false
        property bool first: false
        property bool last: false
        signal clicked()
        implicitWidth: 26
        implicitHeight: 22
        color: checked ? Theme.accent : sm.containsMouse ? Theme.numHover : Theme.num
        radius: first || last ? Theme.radius : 0
        Rectangle { visible: !parent.first; width: parent.radius; height: parent.height; color: parent.color }
        Rectangle { visible: !parent.last; anchors.right: parent.right; width: parent.radius; height: parent.height; color: parent.color }
        Icon { anchors.centerIn: parent; name: parent.icon; size: 15; color: parent.checked ? "#e8eef8" : Theme.text }
        MouseArea { id: sm; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked() }
        ToolTip.visible: tooltip !== "" && sm.containsMouse
        ToolTip.text: tooltip
        ToolTip.delay: 500
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 6; anchors.rightMargin: 6
        spacing: 3
        Drop {
            icon: root.pose ? "bone" : root.gz.target === "camera" ? "video" : "box"
            text: root.pose ? "Pose Mode" : root.gz.target === "camera" ? "Camera Mode" : "Object Mode"
            tooltip: "Object Mode moves whole characters and props; Pose Mode turns bones (Ctrl+Tab)"
            onClicked: root.modeMenu()
        }
        Item { width: 6 }
        MenuWord { text: "View"; onClicked: root.viewMenu() }
        MenuWord { text: "Select"; onClicked: root.selectMenu() }
        MenuWord { text: "Add"; onClicked: root.addMenu() }
        MenuWord { text: root.pose ? "Pose" : "Object"; onClicked: root.objectMenu() }
        Item { Layout.fillWidth: true }

        Drop {
            icon: root.gz.mode === "local" ? "box" : "globe"
            text: root.gz.mode === "local" ? "Local" : "Global"
            tooltip: "Transform orientation (X)"
            onClicked: { const r = studio.popup([{ header: "Transform orientation (X)" }, { text: "Global", value: "world", checked: root.gz.mode !== "local" }, { text: "Local", value: "local", checked: root.gz.mode === "local" }]); if (r) s.send("gizmo", { mode: r }) }
        }
        Row {
            spacing: 0
            Seg { first: true; icon: "magnet"; checked: (root.gz.snap || 1) > 1; tooltip: "Snap while moving and turning"; onClicked: s.send("gizmo", { snap: (root.gz.snap || 1) > 1 ? 1 : 2 }) }
            Seg { last: true; icon: "chevron-down"; tooltip: "Snap increments"; onClicked: root.snapMenu() }
        }
        Item { Layout.fillWidth: true }

        // live mode: the game's own ways in
        IconButton {
            visible: !studio.standalone
            icon: "square-dashed-mouse-pointer"; tooltip: "Edit in the picture (F2 in the game)"
            checked: !!(root.st.edit && root.st.edit.on)
            onClicked: { const on = !(root.st.edit && root.st.edit.on); s.send("edit_mode", { value: on }); if (on) studio.focusGame() }
        }
        IconButton { visible: !studio.standalone; icon: "gamepad-2"; tooltip: "Mouse and keyboard to the game (F1)"; checked: studio.gameFocused; onClicked: studio.toggleFocus() }
        Drop {
            icon: "video"
            text: s.viewLabel
            edge: s.viewMode === "work" || s.viewMode === "game" ? "#3d3d3d" : Theme.amber
            tooltip: "The camera the view looks through: the Work Camera (free, never rendered), the film's cuts, or a camera (Numpad 0)"
            onClicked: root.cameraMenu()
        }
        Item { width: 4 }
        Row {
            spacing: 0
            Seg { first: true; icon: "layers"; checked: !!(s.live && root.st.skeleton); tooltip: "Overlays: the skeleton (Shift+B)"; onClicked: s.send("overlay_skeleton", { value: !root.st.skeleton }) }
            Seg { last: true; icon: "chevron-down"; tooltip: "Viewport overlays, guides, letterbox"; onClicked: root.overlayMenu() }
        }
        Item { width: 4 }
        // shading: wireframe, solid, material preview, rendered (Blender's four spheres, Z)
        Row {
            visible: studio.standalone
            spacing: 0
            Seg { first: true; icon: "shading-wire"; checked: studio.shading === "wire"; tooltip: "Wireframe"; onClicked: studio.shading = "wire" }
            Seg { icon: "shading-solid"; checked: studio.shading === "solid"; tooltip: "Solid: the shapes under the Studio's light"; onClicked: studio.shading = "solid" }
            Seg { icon: "shading-material"; checked: studio.shading === "material"; tooltip: "Material Preview: textures under the Studio's light"; onClicked: studio.shading = "material" }
            Seg { last: true; icon: "shading-rendered"; checked: studio.shading === "rendered"; tooltip: "Rendered: the film's lights, the map's lights, sun, sky and look (what renders)"; onClicked: studio.shading = "rendered" }
        }
    }
}
