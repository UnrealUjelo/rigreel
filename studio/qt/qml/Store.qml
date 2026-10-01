import QtQuick
import "Names.js" as N

// Derived runtime state every panel needs (one instance per panel: `Store { id: s }`).
QtObject {
    readonly property var st: studio.state
    readonly property var dt: studio.data
    // the Studio always hands out a valid state object (empty while the game is not connected)
    readonly property bool live: !!st && !!st.game
    readonly property var seq: live ? st.sequence : ({ t: 0, fps: 60, length: 600, tracks: [], shots: [] })
    readonly property var sel: live ? st.selection : ({})
    readonly property var actors: live ? st.actors : []
    readonly property var cameras: live ? st.cameras : []
    readonly property var lights: live ? st.lights : []
    readonly property var actor: live && sel.actor ? N.find(actors, function (a) { return a.id === sel.actor }) : null
    readonly property var camera: live && sel.camera ? N.find(cameras, function (c) { return c.i === sel.camera }) : null
    readonly property var light: live && sel.light ? N.find(lights, function (l) { return l.id === sel.light }) : null
    readonly property var clipSel: live && sel.clip ? sel.clip : null
    readonly property int t: Math.floor(seq.t || 0)
    readonly property int fps: seq.fps || 60
    readonly property var range: seq.range && seq.range.length === 2 ? seq.range : null
    readonly property var shot: {
        const list = seq.shots || []
        for (let i = 0; i < list.length; ++i) if (seq.t >= list[i].a && seq.t < list[i].b) return Object.assign({ n: i + 1 }, list[i])
        return null
    }
    readonly property string viewMode: live ? (st.camera_view || (st.camera_override ? (st.active_camera === 0 ? "work" : "camera") : "game")) : "game"
    readonly property var liveCamera: live && st.camera_override && st.active_camera ? N.find(cameras, function (c) { return c.i === st.active_camera }) : null
    readonly property string viewLabel: viewMode === "work" ? "Work Camera"
        : viewMode === "scene" ? "Scene Camera" + (liveCamera ? " · " + liveCamera.name : "")
        : liveCamera ? liveCamera.name : "Game Camera"

    function cmd(op, args) { studio.cmd(op, args || {}) }
    function send(op, args) { studio.send(op, args || {}) }
    function actorById(id) { return N.find(actors, function (a) { return a.id === id }) }
    function cameraName(i) { const c = N.find(cameras, function (x) { return x.i === i }); return c ? c.name : "camera " + i }
    function track(idx) { return N.find(seq.tracks, function (x) { return x.idx === idx }) }
    function label(a) { return N.actorLabel(a) }
}
