import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Animation Set Editor (SFM): every animation set in the film - cameras, lights, characters, props - as a tree.
// Open a set to see its controls (root transform, look-at, bones grouped by body part, constraints); pick a control
// and its sliders appear underneath. Click = select, Shift+click = add to the selection, eye = show / hide.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    property string filter: ""
    property var open: ({ "h:cams": true, "h:lights": true, "h:cast": true, "h:props": true })
    function isOpen(k) { return !!open[k] }
    function toggle(k) { const o = Object.assign({}, open); o[k] = !o[k]; open = o }
    function expand(k) { if (!open[k]) toggle(k) }

    KeyedModel { id: rowsModel }

    // the flattened tree: [{ key, kind, depth, label, sub, icon, ... }]
    function buildRows() {
        const out = []
        const f = filter.trim().toLowerCase()
        const match = (t) => !f || String(t).toLowerCase().indexOf(f) >= 0
        const sel = s.sel || {}
        const multi = sel.multi || []
        const head = (key, label, icon, count, add) => out.push({ key: key, kind: "header", depth: 0, label: label, icon: icon, sub: String(count), open: isOpen(key), add: add })

        // cameras
        head("h:cams", "Cameras", "video", s.cameras.length, "camera")
        if (isOpen("h:cams")) {
            if (!s.cameras.length) out.push({ key: "e:cams", kind: "empty", depth: 1, label: "Frame the view and press C (or +) for a camera" })
            for (const c of s.cameras) {
                if (!match(c.name)) continue
                const k = "cam:" + c.i
                const mode = c.mode === "static" ? "" : c.mode === "orbit" ? "orbits " + (c.target_name || "") : c.mode === "follow" ? "follows " + (c.target_name || "") : "aims at " + (c.target_name || "")
                out.push({ key: k, kind: "camera", depth: 1, id: c.i, label: c.name, sub: mode, icon: "video", open: isOpen(k), live: c.live, selected: sel.camera === c.i && Ui.control === "cam:lens" })
                if (isOpen(k))
                    for (const ctl of [["lens", "Lens · " + c.fov.toFixed(0) + "°  " + N.fovToMm(c.fov).toFixed(0) + " mm"], ["transform", "Transform"], ["dof", "Depth of field" + (c.dof_f ? " · f/" + c.dof_f.toFixed(1) : "")], ["motion", "Roll & shake"]])
                        out.push({ key: k + ":" + ctl[0], kind: "control", depth: 2, owner: "camera", id: c.i, control: "cam:" + ctl[0], label: ctl[1], icon: "sliders-horizontal", selected: sel.camera === c.i && Ui.control === "cam:" + ctl[0] })
            }
        }
        // lights
        head("h:lights", "Lights", "lightbulb", s.lights.length, "light")
        if (isOpen("h:lights")) {
            if (!s.lights.length) out.push({ key: "e:lights", kind: "empty", depth: 1, label: "The game's own lighting · + adds a spot or point light" })
            for (const l of s.lights) {
                if (!match(l.name)) continue
                out.push({ key: "light:" + l.id, kind: "light", depth: 1, id: l.id, label: l.name, sub: l.kind + (l.enabled ? "" : " · off"), icon: "lightbulb", on: l.enabled, selected: sel.light === l.id && Ui.control === "light" })
            }
        }
        // characters and props
        const cast = s.actors.filter(a => a.kind !== "object"), props = s.actors.filter(a => a.kind === "object")
        for (const grp of [["h:cast", "Characters", "user", cast, "character"], ["h:props", "Props", "box", props, "prop"]]) {
            head(grp[0], grp[1], grp[2], grp[3].length, grp[4])
            if (!isOpen(grp[0])) continue
            if (!grp[3].length) out.push({ key: "e:" + grp[0], kind: "empty", depth: 1, label: grp[4] === "character" ? "Nobody yet · + or Asset Browser ▸ Characters" : "No props · Asset Browser ▸ Props, or click one in the picture" })
            for (const a of grp[3]) {
                const name = N.actorLabel(a)
                if (!match(name)) continue
                const k = "actor:" + a.id
                const isSel = sel.actor === a.id
                const sub = a.attached ? "on " + a.attached.actor_name + " · " + a.attached.joint : a.spawned ? "" : a.is_player ? "player" : "game"
                out.push({ key: k, kind: "actor", depth: 1, id: a.id, label: name, sub: sub + (a.puppet ? " · AI off" : ""), icon: a.kind === "object" ? (a.attached ? "link-2" : "box") : "user",
                           open: isOpen(k), hidden: !!a.hidden, spawned: !!a.spawned, selected: isSel && (Ui.control === "" || Ui.control === "set"), multi: multi.indexOf(a.id) >= 0 })
                if (!isOpen(k)) continue
                out.push({ key: k + ":root", kind: "control", depth: 2, owner: "actor", id: a.id, control: "root", label: "rootTransform", icon: "move-3d", selected: isSel && Ui.control === "root" })
                if (a.kind === "object") continue
                out.push({ key: k + ":lookat", kind: "control", depth: 2, owner: "actor", id: a.id, control: "lookat", label: "Look at" + (a.lookat && a.lookat.enabled ? " · on" : ""), icon: "eye", selected: isSel && Ui.control === "lookat" })
                const ncons = (s.st && s.st.constraints ? s.st.constraints : []).filter(c => c.actor === a.id).length
                out.push({ key: k + ":cons", kind: "control", depth: 2, owner: "actor", id: a.id, control: "constraints", label: "Constraints" + (ncons ? " · " + ncons : ""), icon: "link", selected: isSel && Ui.control === "constraints" })
                out.push({ key: k + ":presets", kind: "control", depth: 2, owner: "actor", id: a.id, control: "presets", label: "Pose presets", icon: "wand-sparkles", selected: isSel && Ui.control === "presets" })
                if (!isSel) { out.push({ key: k + ":hint", kind: "empty", depth: 2, label: "Select it to list its bones" }); continue }
                const joints = s.dt ? s.dt.joints || [] : []
                const posedJoint = a.pose ? a.pose.joint : null
                for (const g of N.groupedJoints(joints)) {
                    const gk = k + ":g:" + g.key
                    out.push({ key: gk, kind: "group", depth: 2, label: g.label, sub: String(g.joints.length), icon: "bone", open: isOpen(gk) })
                    if (!isOpen(gk)) continue
                    for (const j of g.joints)
                        out.push({ key: gk + ":" + j, kind: "joint", depth: 3, id: a.id, name: j, label: j, icon: "circle", selected: posedJoint === j && Ui.control === "joint" })
                }
            }
        }
        rowsModel.setItems(out)
    }
    Timer { id: rebuild; interval: 30; onTriggered: root.buildRows() }
    Connections { target: studio; function onStateChanged() { rebuild.start() } function onDataChanged() { rebuild.start() } }
    onOpenChanged: rebuild.start()
    onFilterChanged: rebuild.start()
    Connections { target: Ui; function onControlChanged() { rebuild.start() } }
    Component.onCompleted: buildRows()

    function pick(row, shift) {
        if (row.kind === "header" || row.kind === "group") { toggle(row.key); return }
        if (row.kind === "camera") { s.send("cam_select", { i: row.id }); Ui.control = "cam:lens"; Ui.propTab = "camera"; return }
        if (row.kind === "light") { s.send("light_select", { id: row.id }); Ui.control = "light"; Ui.propTab = "light"; return }
        if (row.kind === "actor") { s.send("select_actor", { addr: row.id, add: shift }); Ui.control = "set"; return }
        if (row.kind === "control") {
            if (row.owner === "camera") s.send("cam_select", { i: row.id })
            else if (!s.actor || s.actor.id !== row.id) s.send("select_actor", { addr: row.id })
            Ui.control = row.control
            return
        }
        if (row.kind === "joint") {
            if (!s.actor || s.actor.id !== row.id) s.send("select_actor", { addr: row.id })
            s.send("select_joint", { name: row.name })
            s.send("gizmo", { target: "bone", tool: "rotate" })
            Ui.control = "joint"
        }
    }
    function addMenu(what) {
        const items = []
        if (!what || what === "camera") items.push({ header: "Camera" }, { text: "Camera from this view   (C)", value: "cam" },
            { text: "On the selected character", enabled: !!s.actor, items: [["close", "Close-up"], ["medium", "Medium shot"], ["hero", "Low hero shot"], ["wide", "Wide shot"], ["track", "Tracking shot"]].map(p => ({ text: p[1], value: "preset:" + p[0] })).concat([{ separator: true }, { text: "Follow camera", value: "follow" }, { text: "Orbit camera", value: "orbit" }, { text: "Aim camera", value: "lookat" }]) })
        if (!what || what === "light") items.push({ header: "Light" }, { text: "Spot light", value: "spot" }, { text: "Point light", value: "point" })
        if (!what || what === "character" || what === "prop") {
            items.push({ header: what === "prop" ? "Prop" : "Character" })
            items.push({ text: what === "prop" ? "Browse props…" : "Browse characters…", value: what === "prop" ? "browse:props" : "browse:characters" })
            const scene = (s.dt ? s.dt.scene : []).filter(c => !c.added && (what !== "prop"))
            if (scene.length) items.push({ text: "From the game world", items: scene.slice(0, 40).map(c => ({ text: N.actorName(c.name, c.is_player) + (c.dist >= 0 ? "  ·  " + c.dist.toFixed(0) + " m" : ""), value: "scene:" + c.addr })) })
        }
        const r = studio.popup(items)
        if (!r) return
        if (r === "cam") s.cmd("cam_capture")
        else if (r.indexOf("preset:") === 0) s.cmd("cam_preset", { kind: r.slice(7), addr: s.actor.id })
        else if (r === "follow" || r === "orbit" || r === "lookat") s.cmd("cam_" + r)
        else if (r === "spot" || r === "point") s.cmd("light_add", { kind: r })
        else if (r.indexOf("browse:") === 0) { Ui.assetTab = r.slice(7); Ui.focusAssets(r.slice(7)) }
        else if (r.indexOf("scene:") === 0) s.send("select_actor", { addr: parseFloat(r.slice(6)) })
    }
    function rowMenu(row) {
        let items = []
        if (row.kind === "camera") {
            const c = N.find(s.cameras, x => x.i === row.id)
            items = [{ text: c && c.live ? "Stop looking through it" : "Look through it", value: "live" }, { text: "Cut to it here", value: "cut" }, { text: "Key it here   (M)", value: "key" },
                     { text: "Fly it", value: "fly" }, { text: "Rename…", value: "rename" }, { text: "Recapture from the view", value: "recap" }, { separator: true }, { text: "Delete camera", value: "del" }]
            const r = studio.popup(items)
            if (r === "live") s.send("cam_live", { i: c.live ? 0 : c.i })
            else if (r === "cut") s.cmd("add_cut", { cam: c.i, t: s.t })
            else if (r === "key") s.cmd("key_camera", { i: c.i, t: s.t })
            else if (r === "fly") { s.send("cam_live", { i: c.i }); s.send("cam_fly", { value: true }); studio.focusGame() }
            else if (r === "rename") { const n = studio.prompt("Rename camera", "Name:", c.name); if (n) s.cmd("cam_update", { i: c.i, fields: { name: n } }) }
            else if (r === "recap") s.cmd("cam_recapture", { i: c.i })
            else if (r === "del") s.cmd("cam_remove", { i: c.i })
        } else if (row.kind === "light") {
            const l = N.find(s.lights, x => x.id === row.id)
            const r = studio.popup([{ text: l.enabled ? "Turn off" : "Turn on", value: "toggle" }, { text: "Aim at the selected character", value: "aim", enabled: !!s.actor }, { text: "Move to the view", value: "view" },
                                    { text: "Rename…", value: "rename" }, { separator: true }, { text: "Delete light", value: "del" }])
            if (r === "toggle") s.cmd("light_update", { id: l.id, fields: { enabled: !l.enabled } })
            else if (r === "aim") s.cmd("light_aim", { id: l.id })
            else if (r === "view") s.cmd("light_to_camera", { id: l.id })
            else if (r === "rename") { const n = studio.prompt("Rename light", "Name:", l.name); if (n) s.cmd("light_update", { id: l.id, fields: { name: n } }) }
            else if (r === "del") s.cmd("light_remove", { id: l.id })
        } else if (row.kind === "actor") {
            const a = s.actorById(row.id)
            if (!a) return
            s.send("select_actor", { addr: a.id })
            items = [{ header: N.actorLabel(a) }]
            if (a.kind !== "object") items.push({ text: "Animation track", value: "anim" }, { text: "Key position here   (K)", value: "key" }, { text: "Drive it (WASD)", value: "drive" },
                                                { text: "Camera on it", items: [["close", "Close-up"], ["medium", "Medium"], ["wide", "Wide"], ["track", "Tracking"]].map(p => ({ text: p[1], value: "preset:" + p[0] })) })
            items.push({ text: a.hidden ? "Show" : "Hide", value: "vis" }, { text: "Move to the view", value: "tocam" })
            items.push({ separator: true })
            if (a.spawned) items.push({ text: "Remove from the film   (Delete)", value: "del" })
            else items.push({ text: "Give back to the game", value: "release" })
            const r = studio.popup(items)
            if (!r) return
            if (r === "anim") s.cmd("add_track", { addr: a.id, kind: "anim", layer: 0 })
            else if (r === "key") s.cmd("key_transform", { t: s.t })
            else if (r === "drive") { s.send("drive", { value: true, addr: a.id }); studio.focusGame() }
            else if (r.indexOf("preset:") === 0) s.cmd("cam_preset", { kind: r.slice(7), addr: a.id })
            else if (r === "vis") s.send("actor_visible", { addr: a.id, value: !!a.hidden })
            else if (r === "tocam") s.cmd("move_to_camera")
            else if (r === "del") s.cmd("destroy_object", { addr: a.id })
            else if (r === "release") s.send(a.kind === "object" ? "forget_actor" : "release_actor", { addr: a.id })
        } else if (row.kind === "joint") {
            const r = studio.popup([{ text: "Reset to the animation", value: "reset" }, { text: "Mirror to the other side", value: "mirror", enabled: /^[LR]_/.test(row.name) }, { text: "Key the pose here   (K)", value: "key" }])
            s.send("select_joint", { name: row.name })
            if (r === "reset") s.cmd("reset_joint")
            else if (r === "mirror") s.cmd("mirror_joint")
            else if (r === "key") s.cmd("keyframe_pose", { t: s.t })
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        // toolbar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.bg2
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
            RowLayout {
                anchors.fill: parent
                anchors.margins: 4
                spacing: 4
                IconButton { icon: "plus"; flat: false; tooltip: "Create an animation set: camera, light, character, prop"; onClicked: root.addMenu("") }
                SearchField { Layout.fillWidth: true; placeholderText: "Search…"; onTextChanged: root.filter = text }
                IconButton { icon: "refresh-cw"; tooltip: "Rescan the game world (Ctrl+R)"; onClicked: s.send("refresh") }
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Vertical
            handle: Rectangle { implicitHeight: 4; color: SplitHandle.pressed ? Theme.accent : SplitHandle.hovered ? "#454545" : Theme.line }

            // the sets
            ListView {
                id: list
                SplitView.fillHeight: true
                SplitView.minimumHeight: 120
                clip: true
                model: rowsModel
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                delegate: Rectangle {
                    id: rowItem
                    required property var item
                    required property int index
                    readonly property var r: item
                    readonly property string text: r.label || ""
                    // automation (MCP studio_click): open the set and select it / pick the control
                    function click() { if (["camera", "actor", "group"].indexOf(r.kind) >= 0 && !r.open) root.toggle(r.key); if (r.kind !== "group") root.pick(r, false) }
                    width: ListView.view.width
                    height: 20
                    // Blender's outliner: alternating rows, selected rows blue
                    color: r.selected ? Theme.rowSel : r.multi ? "#2c3f63" : rma.containsMouse ? Theme.rowHover : (index % 2 ? "#2e2e2e" : Theme.bg)
                    HoverHandler { id: rowHover }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 4 + rowItem.r.depth * 14
                        anchors.rightMargin: 4
                        spacing: 5
                        Icon {
                            visible: ["header", "group", "camera", "actor"].indexOf(rowItem.r.kind) >= 0
                            name: rowItem.r.open ? "chevron-down" : "chevron-right"; size: 10; color: Theme.muted
                            MouseArea { anchors.fill: parent; anchors.margins: -4; onClicked: root.toggle(rowItem.r.key) }
                        }
                        Item { visible: ["header", "group", "camera", "actor"].indexOf(rowItem.r.kind) < 0 && rowItem.r.kind !== "empty"; width: 11; height: 1 }
                        Icon {
                            visible: !!rowItem.r.icon
                            name: rowItem.r.icon || ""; size: rowItem.r.kind === "joint" ? 7 : 13
                            // object icons orange, cameras and lights their own colours, collections white (Blender)
                            color: rowItem.r.kind === "camera" ? "#8fc15a" : rowItem.r.kind === "light" ? (rowItem.r.on ? "#e8c85a" : Theme.faint) : rowItem.r.kind === "joint" ? "#6fb0e8"
                                 : rowItem.r.kind === "header" ? "#e5e5e5" : rowItem.r.kind === "actor" ? "#ed9e5c" : "#a5a5a5"
                        }
                        Text {
                            text: rowItem.r.label
                            color: rowItem.r.kind === "empty" ? Theme.faint : rowItem.r.hidden ? Theme.faint : rowItem.r.selected ? Theme.textBright : Theme.text
                            font.pixelSize: rowItem.r.kind === "empty" ? Theme.smallSize : Theme.fontSize
                            font.bold: false
                            font.italic: rowItem.r.kind === "empty"
                            font.family: rowItem.r.kind === "joint" || rowItem.r.label === "rootTransform" ? Theme.mono : ""
                            elide: Text.ElideRight
                            Layout.fillWidth: !rowItem.r.sub
                        }
                        Text { visible: !!rowItem.r.sub; text: rowItem.r.sub || ""; color: Theme.muted; font.pixelSize: Theme.smallSize; elide: Text.ElideRight; Layout.fillWidth: true }
                        Rectangle {
                            visible: !!rowItem.r.live
                            implicitWidth: 36; implicitHeight: 15; radius: 3; color: "#5a4412"; border.color: Theme.amber
                            Text { anchors.centerIn: parent; text: "LIVE"; color: Theme.amber; font.pixelSize: 9; font.bold: true }
                        }
                        IconButton {
                            visible: rowItem.r.kind === "header"
                            icon: "plus"; size: 20; iconSize: 12
                            tooltip: "Add"
                            onClicked: root.addMenu(rowItem.r.add)
                        }
                        IconButton {
                            visible: rowItem.r.kind === "actor" || rowItem.r.kind === "light"
                            icon: rowItem.r.kind === "light" ? (rowItem.r.on ? "eye" : "eye-off") : (rowItem.r.hidden ? "eye-off" : "eye")
                            size: 20; iconSize: 12
                            tooltip: rowItem.r.kind === "light" ? (rowItem.r.on ? "Turn off" : "Turn on") : (rowItem.r.hidden ? "Show" : "Hide in the picture")
                            onClicked: {
                                if (rowItem.r.kind === "light") s.cmd("light_update", { id: rowItem.r.id, fields: { enabled: !rowItem.r.on } })
                                else s.send("actor_visible", { addr: rowItem.r.id, value: rowItem.r.hidden })
                            }
                        }
                        IconButton {
                            visible: rowItem.r.kind === "actor" && rowItem.r.spawned && (rowHover.hovered || !!rowItem.r.selected)
                            icon: "trash-2"; size: 20; iconSize: 12; danger: true
                            tooltip: "Remove from the film (Delete) · Ctrl+Z brings it back"
                            onClicked: s.cmd("destroy_object", { addr: rowItem.r.id })
                        }
                        IconButton {
                            visible: rowItem.r.kind === "camera"
                            icon: "video"; size: 20; iconSize: 12; checked: !!rowItem.r.live
                            tooltip: rowItem.r.live ? "Back to the work camera" : "Look through this camera"
                            onClicked: rowItem.r.live ? s.send("cam_work", { value: true }) : s.send("cam_live", { i: rowItem.r.id })
                        }
                    }
                    MouseArea {
                        id: rma
                        anchors.fill: parent
                        z: -1
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: (m) => { if (m.button === Qt.RightButton) root.rowMenu(rowItem.r); else root.pick(rowItem.r, m.modifiers & Qt.ShiftModifier) }
                        onDoubleClicked: if (["camera", "actor", "group", "header"].indexOf(rowItem.r.kind) >= 0) root.toggle(rowItem.r.key)
                    }
                }
            }

            // the controls of the selected set / control (SFM's slider panel)
            ScrollView {
                id: controls
                SplitView.preferredHeight: 300
                SplitView.minimumHeight: 80
                contentWidth: availableWidth
                clip: true
                background: Rectangle { color: Theme.bg }
                readonly property string c: Ui.control
                Column {
                    width: controls.availableWidth
                    Rectangle {
                        width: parent.width; height: 24; color: Theme.bg2
                        Text {
                            x: 8; anchors.verticalCenter: parent.verticalCenter
                            text: s.camera && controls.c.startsWith("cam:") ? s.camera.name + " · controls" : s.actor ? N.actorLabel(s.actor) + " · " + (controls.c === "joint" ? "bone" : controls.c === "root" ? "rootTransform" : controls.c || "controls") : s.light ? s.light.name : "Controls"
                            color: Theme.muted; font.pixelSize: Theme.smallSize
                        }
                    }
                    Text {
                        visible: !(s.actor && ["set", "root", "", "joint", "lookat", "constraints", "presets"].indexOf(controls.c) >= 0) && !(s.camera && controls.c.startsWith("cam:")) && !(s.light && controls.c === "light")
                        x: 10; width: parent.width - 20; topPadding: 10
                        wrapMode: Text.WordWrap
                        text: "Select something above. Open it (▸) for its controls: rootTransform, look-at, bones by body part. Their sliders appear here."
                        color: Theme.faint; font.pixelSize: Theme.smallSize
                    }
                    JointSection { visible: !!s.actor && controls.c === "joint" }
                    TransformSection { visible: !!s.actor && (controls.c === "root" || controls.c === "set" || controls.c === "") }
                    LookAtSection { visible: !!s.actor && controls.c === "lookat" }
                    ConstraintSection { visible: !!s.actor && controls.c === "constraints" }
                    PoseSection { visible: !!s.actor && (controls.c === "presets" || controls.c === "joint"); open: controls.c === "presets" }
                    CameraSection { visible: !!s.camera && controls.c.startsWith("cam:") }
                    LightSection { visible: !!s.light && controls.c === "light" }
                }
            }
        }
    }
}
