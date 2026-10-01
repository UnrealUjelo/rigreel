.pragma library
// Friendly names for RE4's internal identifiers (raw ids stay available in tooltips).
// Only identities confirmed from the game's own files are named; everything else gets a neutral label with its code.

var CHARACTERS = { cha0: "Leon", cha1: "Ashley", cha2: "Ada", cha3: "Luis", cha8: "Ada (Separate Ways)", chb0: "Merchant", chc0: "Ganado (village)" }
var ROLE = { "0": "player", "1": "enemy", "2": "companion", "3": "player", "8": "mercenaries" }

function humanize(s) {
    s = String(s || "")
    return s.replace(/\.[a-z0-9]+$/i, "").replace(/[_\-]+/g, " ").replace(/\s+/g, " ").trim().replace(/^\w/, function (c) { return c.toUpperCase() })
}

function categoryOf(name) {
    var n = String(name || "").toLowerCase()
    if (/^ch\d[a-z]\d/.test(n)) return "character"
    if (/^wp\d/.test(n)) return "weapon"
    if (/^gm\d/.test(n)) return "gimmick"
    if (/^sm\d|^it_/.test(n)) return "prop"
    return "other"
}

function characterInfo(name) {
    var m = String(name || "").toLowerCase().match(/^ch(\d)([a-z]\d)z(\d)/)
    if (!m) return null
    var code = "ch" + m[2]
    var role = ROLE[m[1]] || ""
    var fallback = role === "enemy" ? "Enemy " + m[2].toUpperCase() : "Character " + m[2].toUpperCase()
    return { code: code, label: CHARACTERS[code] || fallback, role: role }
}

function actorName(name, isPlayer) {
    name = String(name || "")
    if (name.indexOf("Director_") === 0) return name.slice(9).replace(/_+/g, " ").trim()
    var info = characterInfo(name)
    if (!info) return humanize(name)
    if (isPlayer) return info.label
    return info.role && info.role !== "player" ? info.label + " · " + info.role : info.label
}

function actorLabel(a) { return a ? actorName(a.name, a.is_player) : "" }

function objectName(n) {
    return String(n || "").replace(/^(sm|gm|wp|it)\d*[_x]*/i, "").replace(/_+/g, " ").replace(/\s+/g, " ").trim() || n
}

function setLabel(n, c, g) {
    var owner = CHARACTERS[c]
    if (String(g || "").indexOf("cutscene") === 0) {
        var who = String(n).match(/^ch([a-z]\d)/)
        var label = who ? (CHARACTERS["ch" + who[1]] || n) : n
        return "Cutscene " + c + " · " + label + (n.indexOf("_") >= 0 ? " " + n.split("_").slice(1).join(" ") : "")
    }
    var rest = String(n).replace(/^ch[a-z]\d_?/, "")
    var pretty = humanize(rest || n)
    return owner ? owner + " · " + pretty : pretty
}

function motionLabel(name) {
    var m = String(name || "").match(/^(?:ch[a-z0-9]+_)?(?:[a-z0-9]+_)?(\d{3,4})_(.+)$/i)
    if (m) return { label: humanize(m[2]), id: m[1] }
    return { label: humanize(name), id: "" }
}

function groupLabel(g) {
    var map = { gameplay: "Gameplay", cutscene: "Cutscenes", facial: "Facial", weapon: "Weapons", prop: "Props", gimmick: "Interactions", other: "Other" }
    var base = String(g).replace(/\(.*\)$/, "")
    var suffix = g.indexOf("(sw)") >= 0 ? " · Separate Ways" : g.indexOf("(merc)") >= 0 ? " · Mercenaries" : ""
    return (map[base] || humanize(base)) + suffix
}

function castFamily(code, dlc) {
    if (dlc) return "Separate Ways / Mercenaries"
    var f = String(code)[2]
    return f === "a" ? "Main cast" : f === "b" ? "People & bosses" : f === "c" ? "Villagers" : f === "d" ? "Enemies" : (f === "e" || f === "f") ? "Creatures & bosses" : f === "g" ? "Animals" : "Other"
}

// SFM-style grouping of a skeleton's joints into control groups
var GROUPS = [
    { key: "head", label: "Head & Neck", re: /^(Neck|Head)/i },
    { key: "face", label: "Face", re: /(Eye|Jaw|Lip|Brow|Cheek|Tongue|Nose|Ear|Mouth|Lid|Face|Chin|Teeth)/ },
    { key: "spine", label: "Spine & Hips", re: /^(Hip|Spine|Waist|Chest|COG|Pelvis)/i },
    { key: "larm", label: "Left Arm", re: /^L_(Shoulder|Clavicle|UpperArm|Upperarm|Forearm|Elbow|arm_clavicle|arm_humerus|arm_radius)/i },
    { key: "lhand", label: "Left Hand", re: /^L_(Hand|Thumb|Index|Middle|Ring|Pinky|Finger|Palm|Wep|arm_wrist|weapon)/i },
    { key: "rarm", label: "Right Arm", re: /^R_(Shoulder|Clavicle|UpperArm|Upperarm|Forearm|Elbow|arm_clavicle|arm_humerus|arm_radius)/i },
    { key: "rhand", label: "Right Hand", re: /^R_(Hand|Thumb|Index|Middle|Ring|Pinky|Finger|Palm|Wep|arm_wrist|weapon)/i },
    { key: "lleg", label: "Left Leg", re: /^L_(Thigh|Shin|Knee|Leg|Calf|Foot|Toe|Ankle)/i },
    { key: "rleg", label: "Right Leg", re: /^R_(Thigh|Shin|Knee|Leg|Calf|Foot|Toe|Ankle)/i },
]
function jointGroup(name) {
    if (/Twist|Null|Offset|Helper|Aux|^root$|^_|muscle|End$/i.test(name)) return "helpers"
    for (var i = 0; i < GROUPS.length; ++i) if (GROUPS[i].re.test(name)) return GROUPS[i].key
    return "other"
}
function groupedJoints(joints) {
    var out = {}
    for (var i = 0; i < GROUPS.length; ++i) out[GROUPS[i].key] = []
    out.other = []; out.helpers = []
    for (var j = 0; j < joints.length; ++j) out[jointGroup(joints[j])].push(joints[j])
    var list = []
    for (var g = 0; g < GROUPS.length; ++g) if (out[GROUPS[g].key].length) list.push({ key: GROUPS[g].key, label: GROUPS[g].label, joints: out[GROUPS[g].key] })
    if (out.other.length) list.push({ key: "other", label: "Hair, Cloth & Other", joints: out.other })
    if (out.helpers.length) list.push({ key: "helpers", label: "Helpers", joints: out.helpers })
    return list
}

function fovToMm(fov) { return 18 / Math.tan((fov * Math.PI) / 360) }
function mmToFov(mm) { return (Math.atan(18 / Math.max(1, mm)) * 360) / Math.PI }

function hexOf(rgb) {
    function h(v) { var s = Math.round(Math.max(0, Math.min(1, v)) * 255).toString(16); return s.length < 2 ? "0" + s : s }
    return "#" + h(rgb[0]) + h(rgb[1]) + h(rgb[2])
}
function rgbOf(hex) { return [parseInt(hex.slice(1, 3), 16) / 255, parseInt(hex.slice(3, 5), 16) / 255, parseInt(hex.slice(5, 7), 16) / 255] }

function find(list, pred) { if (!list) return null; for (var i = 0; i < list.length; ++i) if (pred(list[i])) return list[i]; return null }
