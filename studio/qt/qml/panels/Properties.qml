import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Director
import "../Names.js" as N

// Properties: every setting, organised like Blender's properties editor - a rail of tabs on the left.
// The top four belong to the film as a whole (Tool, Render, Output, World); the rest follow the selection
// (Object, Animation, Physics, Constraints, Character, Camera, Light, Clip). A new selection opens its tab.
Rectangle {
    id: root
    signal closeRequested()
    color: Theme.bg
    Store { id: s }

    readonly property var tabs: {
        const list = [
            { v: "tool", icon: "wrench", label: "Tool — manipulator, snapping, overlays", group: 0 },
            { v: "render", icon: "clapperboard", label: "Render — export the movie", group: 0 },
            { v: "output", icon: "film", label: "Output — length, time selection, shots", group: 0 },
            { v: "world", icon: "globe", label: "World — stage, area, game sound", group: 0 },
        ]
        if (s.actor) {
            list.push({ v: "object", icon: "move-3d", label: "Object — transform and visibility", group: 1 })
            if (s.actor.kind !== "object") {
                list.push({ v: "anim", icon: "film", label: "Animation — clips, layers, walk path, look-at", group: 1 })
                list.push({ v: "data", icon: "bone", label: "Character — look, bones, pose presets", group: 1 })
            }
            list.push({ v: "physics", icon: "orbit", label: "Physics — ragdoll, cloth, world", group: 1 })
            list.push({ v: "constraint", icon: "link", label: "Constraints — hold, plant, aim", group: 1 })
        }
        if (s.camera) list.push({ v: "camera", icon: "video", label: "Camera — lens, focus, movement", group: 1 })
        if (s.light) list.push({ v: "light", icon: "lightbulb", label: "Light — colour, shadows, falloff", group: 1 })
        if (s.clipSel) list.push({ v: "clip", icon: "layers", label: s.clipSel.kind === "camera" ? "Cut" : "Clip", group: 1 })
        return list
    }
    readonly property string tab: N.find(tabs, t => t.v === Ui.propTab) ? Ui.propTab : "tool"

    // a new selection jumps to the tab that describes it
    property var prev: ({})
    function follow() {
        const k = s.clipSel ? s.clipSel.track + ":" + s.clipSel.id : null
        const a = s.actor ? s.actor.id : null, c = s.camera ? s.camera.i : null, l = s.light ? s.light.id : null
        let next = null
        if (k !== prev.k && k) next = "clip"
        else if (l !== prev.l && l) next = "light"
        else if (c !== prev.c && c) next = "camera"
        else if (a !== prev.a && a) next = (Ui.propTab === "anim" || Ui.propTab === "data" || Ui.propTab === "physics" || Ui.propTab === "constraint") ? Ui.propTab : "object"
        prev = { a: a, c: c, l: l, k: k }
        if (next) Ui.propTab = next
    }
    Connections { target: studio; function onStateChanged() { root.follow() } }

    readonly property string heading: {
        if (["object", "anim", "physics", "constraint", "data"].indexOf(tab) >= 0) return s.actor ? N.actorLabel(s.actor) : ""
        if (tab === "camera") return s.camera ? s.camera.name : ""
        if (tab === "light") return s.light ? s.light.name : ""
        const t = N.find(tabs, x => x.v === tab)
        return t ? t.label.split(" — ")[0] : ""
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        // the rail
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 34
            color: "#232323"
            Column {
                y: 4
                width: parent.width
                spacing: 2
                Repeater {
                    model: root.tabs
                    Column {
                        required property var modelData
                        required property int index
                        width: 34
                        Item { visible: index > 0 && root.tabs[index - 1].group !== modelData.group; width: 1; height: 8 }
                        // Blender's property tabs: a rounded grey square behind the open one
                        Rectangle {
                            x: 4
                            width: 26; height: 26; radius: 5
                            readonly property bool on: root.tab === parent.modelData.v
                            color: on ? Theme.num : tabMa.containsMouse ? "#3a3a3a" : "transparent"
                            Icon { anchors.centerIn: parent; name: parent.parent.modelData.icon; size: 15; color: parent.on ? "#ffffff" : "#bababa" }
                            MouseArea { id: tabMa; anchors.fill: parent; hoverEnabled: true; onClicked: Ui.propTab = parent.parent.modelData.v }
                            ToolTip.visible: tabMa.containsMouse
                            ToolTip.text: parent.modelData.label
                            ToolTip.delay: 500
                        }
                    }
                }
            }
        }
        // the page (preferred width 1: take the width the dock gives, never what the content would like)
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 28
                color: Theme.bg2
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.line }
                Row {
                    x: 10; anchors.verticalCenter: parent.verticalCenter
                    spacing: 6
                    Icon { name: (N.find(root.tabs, t => t.v === root.tab) || {}).icon || "wrench"; size: 13; color: Theme.muted; anchors.verticalCenter: parent.verticalCenter }
                    Text { text: root.heading; color: Theme.text; font.pixelSize: Theme.fontSize; elide: Text.ElideRight; anchors.verticalCenter: parent.verticalCenter }
                }
            }
            GroupBar { Layout.fillWidth: true }
            ScrollView {
                id: sv
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 1
                contentWidth: availableWidth
                clip: true
                Column {
                    width: sv.availableWidth
                    spacing: 0
                    // film-wide
                    ToolPage { visible: root.tab === "tool" }
                    RenderPage { visible: root.tab === "render" }
                    OutputPage { visible: root.tab === "output" }
                    WorldPage { visible: root.tab === "world" }
                    // selection
                    TransformSection { visible: root.tab === "object" && !!s.actor }
                    ObjectSection { visible: root.tab === "object" && !!s.actor && s.actor.kind === "object" }
                    AnimationSection { visible: root.tab === "anim" && !!s.actor && s.actor.kind !== "object" }
                    PathSection { visible: root.tab === "anim" && !!s.actor && s.actor.kind !== "object" }
                    LookAtSection { visible: root.tab === "anim" && !!s.actor && s.actor.kind !== "object"; open: false }
                    CastSection { visible: root.tab === "data" && !!s.actor && !!s.actor.cast }
                    JointSection { visible: root.tab === "data" && !!s.actor && s.actor.kind !== "object" }
                    PoseSection { visible: root.tab === "data" && !!s.actor }
                    PhysicsSection { visible: root.tab === "physics" && !!s.actor }
                    ConstraintSection { visible: root.tab === "constraint" && !!s.actor }
                    CameraSection { visible: root.tab === "camera" && !!s.camera }
                    LightSection { visible: root.tab === "light" && !!s.light }
                    ClipSection { visible: root.tab === "clip" && !!s.clipSel }
                    Item { width: 1; height: 30 }
                }
            }
        }
    }

    // ------------------------------------------------------------------ film-wide pages
    component ToolPage: Column {
        id: toolPage
        width: parent ? parent.width : 300
        Store { id: s }   // inline components do not see the file's ids
        readonly property var gz: s.live ? s.st.gizmo : ({})
        readonly property var ov: s.live ? s.st.overlay : ({})
        Section {
            title: "Moving things"; icon: "mouse-pointer-2"
            Field { label: "Manipulator on"; Choice { Layout.fillWidth: true; value: toolPage.gz.target || "actor"; options: [{ v: "actor", label: "the selected thing" }, { v: "camera", label: "the camera" }, { v: "bone", label: "a bone" }]; onChosen: (v) => s.send("gizmo", { target: v }) } }
            Field { label: "Tool"; Choice { Layout.fillWidth: true; value: s.live && s.st.gizmo.enabled ? s.st.gizmo.op : "select"; options: [{ v: "select", label: "Select (Q)" }, { v: "move", label: "Move (W)" }, { v: "rotate", label: "Rotate (E)" }, { v: "scale", label: "Scale (R)" }]; onChosen: (v) => s.send("gizmo", v === "select" ? { enabled: false } : { enabled: true, tool: v }) } }
            Field { label: "Axes"; Choice { Layout.fillWidth: true; value: s.live ? s.st.gizmo.mode : "world"; options: [{ v: "world", label: "World" }, { v: "local", label: "Local (object)" }]; onChosen: (v) => s.send("gizmo", { mode: v }) } }
            Field { label: "Snapping"; Choice { Layout.fillWidth: true; value: s.live ? s.st.gizmo.snap || 1 : 1; options: [{ v: 1, label: "Off" }, { v: 2, label: "0.1 m · 15°" }, { v: 3, label: "0.5 m · 45°" }, { v: 4, label: "1 m · 90°" }]; onChosen: (v) => s.send("gizmo", { snap: v }) } }
            Toggle { visible: !studio.standalone; text: "Click props in the picture (chairs, barrels, lamps)"; checked: s.live && !!s.st.pick_props; onToggled: (v) => s.send("pick_props", { value: v }) }
            Toggle { visible: !studio.standalone; text: "IK handles on the selected character"; checked: s.live && (!s.st.edit || s.st.edit.ik_handles !== false); onToggled: (v) => s.send("ik_handles", { value: v }) }
            RowLayout {
                Btn { visible: !studio.standalone; kind: s.live && s.st.edit && s.st.edit.on ? "primary" : ""; text: s.live && s.st.edit && s.st.edit.on ? "Leave edit mode" : "Edit in picture (F2 in game)"; onClicked: { const on = !(s.st.edit && s.st.edit.on); s.send("edit_mode", { value: on }); if (on) studio.focusGame() } }
                Btn { kind: s.live && s.st.autokey ? "danger" : ""; text: "Auto-key " + (s.live && s.st.autokey ? "on" : "off"); onClicked: s.send("autokey", { value: !s.st.autokey }) }
            }
        }
        Section {
            title: "Overlays"; icon: "layers"
            Toggle { text: "Skeleton"; checked: s.live && !!s.st.skeleton; onToggled: (v) => s.send("overlay_skeleton", { value: v }) }
            Toggle { text: "Onion skin (previous / next pose)"; checked: s.live && !!s.st.onion; onToggled: (v) => s.send("overlay_onion", { value: v }) }
            Field { label: "Letterbox"; Choice { Layout.fillWidth: true; value: s.live ? s.st.overlay.letterbox || 0 : 0; options: [{ v: 0, label: "Off" }, { v: 1.85, label: "1.85 : 1" }, { v: 2, label: "2 : 1" }, { v: 2.39, label: "2.39 : 1 · scope" }]; onChosen: (v) => s.send("cam_overlay", { fields: { letterbox: v } }) } }
            Flow {
                Layout.fillWidth: true; spacing: 14
                Toggle { text: "Thirds"; checked: s.live && !!s.st.overlay.thirds; onToggled: (v) => s.send("cam_overlay", { fields: { thirds: v } }) }
                Toggle { text: "Safe area"; checked: s.live && !!s.st.overlay.safe; onToggled: (v) => s.send("cam_overlay", { fields: { safe: v } }) }
                Toggle { text: "Centre"; checked: s.live && !!s.st.overlay.center; onToggled: (v) => s.send("cam_overlay", { fields: { center: v } }) }
            }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "Letterbox is part of the picture and of renders. The other guides are for you only."; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
    }
    component RenderPage: Column {
        id: renderPage
        width: parent ? parent.width : 300
        Store { id: s }   // inline components do not see the file's ids
        readonly property var rp: studio.render
        Section {
            title: "Render"; icon: "clapperboard"
            Text {
                text: (s.range ? "Time selection: frames " + s.range[0] + "–" + s.range[1] : "The whole film") + " · " + ((s.range ? s.range[1] - s.range[0] : s.seq.length) / s.fps).toFixed(1) + " s"
                color: Theme.muted; font.pixelSize: Theme.smallSize
            }
            RowLayout {
                Btn { kind: "primary"; icon: "clapperboard"; text: "Export movie…  (Ctrl+Shift+M)"; onClicked: studio.showRenderDialog() }
                Btn { kind: "ghost"; text: "Open the folder"; onClicked: studio.openFolder("renders") }
            }
            Text { visible: !!renderPage.rp.running; text: "Rendering " + (renderPage.rp.frame || 0) + " / " + (renderPage.rp.total || 0); color: Theme.amber; font.pixelSize: Theme.fontSize }
            Text { visible: !!renderPage.rp.error; Layout.fillWidth: true; wrapMode: Text.WordWrap; text: renderPage.rp.error || ""; color: Theme.danger; font.pixelSize: Theme.smallSize }
        }
        Section {
            title: "The picture"; icon: "monitor"
            Toggle { text: "Hide the game's HUD"; checked: s.live && !!s.st.hud_hidden; onToggled: (v) => s.send("hud", { value: !v }) }
            Toggle { text: "Hide Leon's weapon"; checked: s.live && !!s.st.hide_weapons; onToggled: (v) => s.send("hide_weapons", { value: v }) }
            Text { text: "Both stay hidden while a render runs."; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
    }
    component OutputPage: Column {
        id: outputPage
        width: parent ? parent.width : 300
        Store { id: s }   // inline components do not see the file's ids
        readonly property real filmFrames: (s.seq.shots || []).reduce((n, x) => n + Math.max(1, x.b - x.a), 0)
        readonly property string filmLabel: "The film: " + (s.seq.shots || []).length + " shots, " + (filmFrames / s.fps).toFixed(1) + " s."
        Section {
            title: "Film"; icon: "film"
            Field { label: "Length"; NumField { Layout.fillWidth: true; step: 1; value: s.seq.length || 600; onEdited: (v) => s.cmd("seq_set", { length: Math.round(v) }) } Text { text: ((s.seq.length || 600) / s.fps).toFixed(1) + " s"; color: Theme.muted; font.pixelSize: Theme.smallSize } }
            Field { label: "Frame rate"; Text { text: s.fps + " fps · the game's own rate (renders pick their own)"; color: Theme.muted; font.pixelSize: Theme.smallSize } }
            Toggle { text: "Loop playback"; checked: !!s.seq.loop; onToggled: (v) => s.send("seq_set", { loop: v }) }
            Field { label: "Preview speed"; Choice { Layout.fillWidth: true; value: s.seq.speed || 1; options: [{ v: 0.25, label: "¼ speed" }, { v: 0.5, label: "Half speed" }, { v: 1, label: "Normal" }, { v: 2, label: "Double" }]; onChosen: (v) => s.send("seq_speed", { value: v }) } }
        }
        Section {
            title: "Time selection"; icon: "scan"
            RowLayout {
                Btn { text: "Start @ " + s.t + "  (I)"; onClicked: s.send("seq_range", { a: s.t }) }
                Btn { text: "End @ " + s.t + "  (O)"; onClicked: s.send("seq_range", { b: s.t }) }
                Btn { visible: !!s.range; kind: "ghost"; text: "Clear"; onClicked: s.send("seq_range", { clear: true }) }
            }
            Text { text: s.range ? "Playing and rendering frames " + s.range[0] + "–" + s.range[1] : "Playing and rendering everything"; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
        Section {
            title: "Shots"; icon: "clapperboard"; note: (s.seq.shots || []).length + ""
            Text { visible: (s.seq.shots || []).length === 0; text: "No shots yet. Set a time selection, then add one."; color: Theme.muted; font.pixelSize: Theme.smallSize }
            Text {
                visible: (s.seq.shots || []).length > 0
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "The film plays the shots in this order, each its own stretch of the scene through its own camera — cover the same action from several cameras and cut between them. " + outputPage.filmLabel
                color: Theme.muted; font.pixelSize: Theme.smallSize
            }
            Repeater {
                model: s.seq.shots || []
                RowLayout {
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Rectangle { width: 3; height: 18; radius: 1; color: s.live && s.st.film && s.st.film.on && s.st.film.shot === modelData.id ? Theme.amber : "transparent" }
                    IconButton { icon: "chevron-up"; size: 18; enabled: index > 0; tooltip: "Earlier in the film"; onClicked: s.cmd("shot_move", { id: modelData.id, by: -1 }) }
                    IconButton { icon: "chevron-down"; size: 18; enabled: index < (s.seq.shots || []).length - 1; tooltip: "Later in the film"; onClicked: s.cmd("shot_move", { id: modelData.id, by: 1 }) }
                    TextBox { Layout.fillWidth: true; text: modelData.name; onCommitted: (t) => s.cmd("shot_update", { id: modelData.id, fields: { name: t } }) }
                    Text { text: modelData.a + "–" + modelData.b; color: Theme.muted; font.family: Theme.mono; font.pixelSize: 10 }
                    Choice { implicitWidth: 90; value: modelData.cam; placeholder: "camera"; options: s.cameras.map(c => ({ v: c.i, label: c.name })); onChosen: (v) => s.cmd("shot_update", { id: modelData.id, fields: { cam: v } }) }
                    IconButton { icon: "play"; size: 22; tooltip: "Enter this shot"; onClicked: s.send("shot_go", { id: modelData.id }) }
                    IconButton { icon: "x"; size: 22; danger: true; onClicked: s.cmd("shot_remove", { id: modelData.id }) }
                }
            }
            RowLayout {
                Btn { icon: "plus"; text: "Add a shot from the time selection"; onClicked: s.cmd("shot_add", {}) }
                Btn {
                    visible: (s.seq.shots || []).length > 0
                    kind: "primary"; icon: s.live && s.st.film && s.st.film.on ? "square" : "play"
                    text: s.live && s.st.film && s.st.film.on ? "Stop the film" : "Play the film"
                    onClicked: s.send(s.live && s.st.film && s.st.film.on ? "film_stop" : "film_play", {})
                }
            }
        }
    }
    component WorldPage: Column {
        id: worldPage
        width: parent ? parent.width : 300
        Store { id: s }   // inline components do not see the file's ids
        readonly property var mix: s.live ? s.st.mix : null
        readonly property var stage: s.live ? s.st.stage : null
        readonly property var area: stage && stage.current && s.dt ? N.find(s.dt.stages, x => x.stage === stage.current.stage) : null
        // standalone: the map's own lighting (one per chapter / time of day in RE4) and the Studio's sun and sky
        readonly property var world: s.live && s.st.world ? s.st.world : ({})
        function setWorld(f) { s.cmd("world_set", { fields: f }) }
        function lightingLabel(v) {
            const m = /^chp(\d+)_(\d+)$/.exec(v)
            return m ? "Chapter " + m[1] + " · lighting " + (parseInt(m[2]) + 1) : v
        }
        Section {
            visible: studio.standalone && !!worldPage.stage && !!worldPage.stage.current
            title: "Map lighting"; icon: "lamp"
            Text {
                Layout.fillWidth: true; wrapMode: Text.WordWrap
                text: "The map's own lamps, windows and fires. RE4 lights each place differently per chapter (day, dusk, night): pick one."
                color: Theme.muted; font.pixelSize: Theme.smallSize
            }
            Field {
                label: "Lighting"
                Choice {
                    Layout.fillWidth: true
                    readonly property var opts: worldPage.stage && worldPage.stage.current ? (worldPage.stage.current.lightings || []) : []
                    value: worldPage.world.lighting || ""
                    options: [{ v: "", label: opts.length ? "Automatic (" + worldPage.lightingLabel(opts[0]) + ")" : "The map's lights" }]
                             .concat(opts.map(o => ({ v: o, label: worldPage.lightingLabel(o) }))).concat([{ v: "none", label: "None (only the Studio's lights)" }])
                    onChosen: (v) => worldPage.setWorld({ lighting: v })
                }
            }
            SliderRow { channel: "world/map_lights"; label: "Strength"; from: 0; to: 5; step: 0.05; value: worldPage.world.map_lights === undefined ? 1 : worldPage.world.map_lights; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ map_lights: v }) }
        }
        Section {
            visible: studio.standalone
            title: "Sun & sky"; icon: "sun"
            Field {
                label: "Sun"
                Chip { text: "Auto"; checked: (worldPage.world.sun || "auto") === "auto"; tooltip: "On while the film has no lights of its own"; onClicked: worldPage.setWorld({ sun: "auto" }) }
                Chip { text: "On"; checked: worldPage.world.sun === "on"; onClicked: worldPage.setWorld({ sun: "on" }) }
                Chip { text: "Off"; checked: worldPage.world.sun === "off"; tooltip: "Night: only the map's lights and the film's own"; onClicked: worldPage.setWorld({ sun: "off" }) }
            }
            SliderRow { channel: "world/sun"; label: "Sun strength"; from: 0; to: 4; step: 0.05; value: worldPage.world.sun_strength === undefined ? 1 : worldPage.world.sun_strength; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ sun_strength: v }) }
            SliderRow { channel: "world/sun_yaw"; label: "Sun direction"; from: -180; to: 180; step: 1; value: worldPage.world.sun_yaw === undefined ? 32 : worldPage.world.sun_yaw; format: (v) => v.toFixed(0) + "°"; onEdited: (v) => worldPage.setWorld({ sun_yaw: v }) }
            SliderRow { channel: "world/sun_elevation"; label: "Sun height"; from: -10; to: 90; step: 0.5; value: worldPage.world.sun_elevation === undefined ? 38 : worldPage.world.sun_elevation; format: (v) => v.toFixed(0) + "°"; onEdited: (v) => worldPage.setWorld({ sun_elevation: v }) }
            SliderRow { channel: "world/sun_temp"; label: "Sun colour"; from: 1800; to: 12000; step: 50; fill: "#8a6a2a"; value: worldPage.world.sun_temp === undefined ? 5600 : worldPage.world.sun_temp; format: (v) => v.toFixed(0) + " K"; onEdited: (v) => worldPage.setWorld({ sun_temp: v }) }
            SliderRow { channel: "world/sky"; label: "Sky light"; from: 0; to: 3; step: 0.05; value: worldPage.world.sky === undefined ? 1 : worldPage.world.sky; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ sky: v }) }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: "The diamonds animate a value over the film (sunset, lights coming on)."; color: Theme.faint; font.pixelSize: Theme.smallSize }
        }
        Section {
            visible: studio.standalone
            title: "Look"; icon: "aperture"
            Field {
                label: "Tone"
                Choice {
                    Layout.fillWidth: true
                    value: worldPage.world.tonemap || "filmic"
                    options: [{ v: "filmic", label: "Filmic" }, { v: "aces", label: "ACES (contrasty)" }, { v: "hejl", label: "Hejl (soft)" }, { v: "linear", label: "None (flat)" }]
                    onChosen: (v) => worldPage.setWorld({ tonemap: v })
                }
            }
            SliderRow { channel: "world/exposure"; label: "Exposure"; from: -4; to: 4; step: 0.05; value: worldPage.world.exposure || 0; format: (v) => (v > 0 ? "+" : "") + v.toFixed(2) + " EV"; onEdited: (v) => worldPage.setWorld({ exposure: v }) }
            SliderRow { channel: "world/contrast"; label: "Contrast"; from: 0.3; to: 2; step: 0.01; value: worldPage.world.contrast === undefined ? 1 : worldPage.world.contrast; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ contrast: v }) }
            SliderRow { channel: "world/saturation"; label: "Saturation"; from: 0; to: 2; step: 0.01; value: worldPage.world.saturation === undefined ? 1 : worldPage.world.saturation; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ saturation: v }) }
            SliderRow { channel: "world/brightness"; label: "Brightness"; from: 0.3; to: 2; step: 0.01; value: worldPage.world.brightness === undefined ? 1 : worldPage.world.brightness; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ brightness: v }) }
            SliderRow { channel: "world/bloom"; label: "Bloom"; from: 0; to: 1; step: 0.01; value: worldPage.world.bloom || 0; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ bloom: v }) }
            SliderRow { channel: "world/vignette"; label: "Vignette"; from: 0; to: 1; step: 0.01; value: worldPage.world.vignette || 0; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ vignette: v }) }
            SliderRow { channel: "world/sharpen"; label: "Sharpen"; from: 0; to: 1; step: 0.01; value: worldPage.world.sharpen || 0; format: (v) => Math.round(v * 100) + " %"; onEdited: (v) => worldPage.setWorld({ sharpen: v }) }
            Btn { kind: "ghost"; text: "Reset the look"; onClicked: worldPage.setWorld({ exposure: 0, contrast: 1, saturation: 1, brightness: 1, bloom: 0, vignette: 0, sharpen: 0, tonemap: "filmic" }) }
        }
        Section {
            visible: !studio.standalone
            title: "Stage"; icon: "globe"
            Toggle { text: "Empty stage (hide the game's characters)"; checked: s.live && !!s.st.stage_clean; onToggled: (v) => s.send("stage_clean", { value: v }) }
            Toggle { text: "Freeze the world"; checked: s.live && !!s.st.game.frozen; onToggled: (v) => s.send("game_freeze", { value: v }) }
            Text { Layout.fillWidth: true; wrapMode: Text.WordWrap; text: worldPage.area ? "You are at " + worldPage.area.name + " · " + worldPage.area.map : "Area unknown — Asset Browser ▸ Stages travels anywhere."; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
        Section {
            visible: !studio.standalone
            title: "Game sound"; icon: "sliders-horizontal"
            Text { visible: !worldPage.mix; text: "Reset the scripts to get the mixer."; color: Theme.muted; font.pixelSize: Theme.smallSize }
            Repeater {
                model: [["master", "Everything"], ["music", "Music"], ["sfx", "Effects"], ["voice", "Voices"], ["cutscene", "Cutscenes"]]
                SliderRow {
                    required property var modelData
                    visible: !!s.live && !!s.st.mix
                    label: modelData[1]
                    value: s.live && s.st.mix ? (s.st.mix[modelData[0]] === undefined ? 1 : s.st.mix[modelData[0]]) : 1
                    step: 0.05
                    format: (v) => Math.round(v * 100) + " %"
                    onEdited: (v) => { const f = {}; f[modelData[0]] = v; s.send("sound_mix", { fields: f }) }
                }
            }
            Btn { kind: "ghost"; text: "Reset the mix"; onClicked: s.send("sound_mix", { reset: true }) }
            Text { text: "What you hear is what a render records."; color: Theme.muted; font.pixelSize: Theme.smallSize }
        }
    }
}
