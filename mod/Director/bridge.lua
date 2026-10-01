-- Director :: bridge — file channel between the Lua runtime and RigReel Studio
--   reframework/data/director/bridge/cmd.json    written by Studio : { seq, cmds = [ {cid, op, ...}, ... ] }
--   reframework/data/director/bridge/state.json  written here ~15x/s : small, changes every frame
--   reframework/data/director/bridge/data.json   written here on change : scene scan, catalog results, motions, joints, log
local Log = require("Director.log")
local E = require("Director.engine")
local Catalog = require("Director.catalog")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Seq = require("Director.sequence")
local Pose = require("Director.pose")
local Lookat = require("Director.lookat")
local Gizmo = require("Director.gizmo")
local Project = require("Director.project")
local Selftest = require("Director.selftest")
local Cast = require("Director.cast")
local Grab = require("Director.grab")
local Edit = require("Director.edit")
local Drive = require("Director.drive")
local History = require("Director.history")
local IK = require("Director.ik")
local Lights = require("Director.lights")
local RM = require("Director.rootmotion")
local Import = require("Director.import")
local X = require("Director.extras")
local Stage = require("Director.stage")
local Constraint = require("Director.constraint")
local PathMod = require("Director.path")

local B = {
    dir = "director/bridge/",
    last_cmd_id = 0,
    state_every = 4,          -- frames
    data_ver = 0, data_dirty = true,
    scan = {}, scan_time = -100,
    catalog = { query = "", group = nil, owner = nil, results = {} },
    sel = { actor = nil, camera = 0, clip = nil },
    joint = nil,              -- selected joint name (poser)
    editor_alive = 0,
}

local function now() return os.clock() end

-------------------------------------------------------------------------------
-- helpers
-------------------------------------------------------------------------------
local function cur_actor()
    local a = B.sel.actor and Actor.get(B.sel.actor)
    if a and a:valid() then return a end
    B.sel.actor = nil
    return nil
end

local function working(a)
    a.work = a.work or { joint = nil, euler = { 0, 0, 0 } }
    local w = a.work
    local attached = false
    for _, l in ipairs(a.pose_layers or {}) do if l == w.layer then attached = true end end
    if not w.layer or not attached then
        w.layer = Pose.add_layer(a, "working pose", "override", 100)
        w.layer.joints = w.saved_joints or {}
    end
    w.saved_joints = w.layer.joints
    return w, w.layer
end

History.working = working
IK.working = working
X.selected = function() return cur_actor() end
Seq.ground_y = X.ground_y
Seq.resolve_motion = X.resolve_motion

Gizmo.ui = function()
    local a = cur_actor()
    local ctx = { actor = a, cam_idx = B.sel.camera }
    if a and Gizmo.target == "bone" then
        local w, layer = working(a)
        ctx.work, ctx.layer = w, layer
    end
    return ctx
end

local function v3t(v) return { v.x, v.y, v.z } end
-- where the user is looking from: the live Director camera when the override is on, else the game camera
local function view_pose()
    if Cam.enabled and Cam.active and Cam.cams[Cam.active] then
        local pos, rot, fov = Cam.evaluate(Cam.cams[Cam.active], 0)
        if pos then return { pos = { pos.x, pos.y, pos.z }, rot = { rot.w, rot.x, rot.y, rot.z }, fov = fov } end
    end
    return Cam.game_pose()
end
B.view_pose = view_pose

B.objects, B.obj_time = {}, -100
local function refresh_scan(force)
    if force or now() - B.scan_time > 3 then
        Log.try("scan", function() B.scan = Actor.scan(nil) end)
        B.scan_time = now()
        B.data_dirty = true
    end
    if B.want_objects and (force or now() - B.obj_time > 6) then
        Log.try("scan objects", function() B.objects = Actor.scan_objects(30, 200) end)
        B.obj_time = now()
        B.data_dirty = true
    end
end

local function actor_by_addr(addr)
    local a = Actor.get(addr)
    if a then return a end
    for _, c in ipairs(B.scan) do
        if c.go:get_address() == addr then return Actor.wrap(c.go) end
    end
    for _, c in ipairs(B.objects or {}) do
        if c.go:get_address() == addr then return Actor.wrap(c.go) end
    end
    return nil
end

-------------------------------------------------------------------------------
-- state (small, frequent)
-------------------------------------------------------------------------------
local function actor_state(a)
    local p = a.xform:call("get_Position")
    local rot = a.xform:call("get_Rotation")
    local ex, ey, ez = E.euler_deg(rot)
    local layers = {}
    for i = 0, math.min(a:layer_count() - 1, 12) do
        local li = a:layer_info(i)
        if li then layers[#layers + 1] = { idx = i, bank = li.bank, mot = li.mot, frame = li.frame, endframe = li.endframe, speed = li.speed,
            name = li.bank and Catalog.motion_name(Catalog.path_for_bank(li.bank) or "", li.mot) or nil } end
    end
    local banks = {}
    for bank_id, b in pairs(a.banks) do
        banks[#banks + 1] = { id = bank_id, path = b.path, name = b.path:match("[^/]+$"), ready = b.ready and true or false, count = b.motions and #b.motions or 0 }
    end
    for bank_id, b in pairs(a.facial_banks or {}) do
        banks[#banks + 1] = { id = bank_id, path = b.path, name = b.path:match("[^/]+$"), ready = b.ready and true or false, count = b.motions and #b.motions or 0, facial = true }
    end
    table.sort(banks, function(x, y) return x.id < y.id end)
    local la = a.lookat or {}
    local w = a.work
    local posed = 0
    if w and w.layer then for _ in pairs(w.layer.joints) do posed = posed + 1 end end
    local sel_joint = w and w.joint
    local jeuler = nil
    if sel_joint and w.layer and w.layer.joints[sel_joint] then jeuler = w.euler end
    local lookat_target = la.target and Actor.get(la.target)
    return {
        id = a.addr, name = a.name, display_name = a.display_name, cast = Cast.describe(a), kind = a.kind or "character", spawned = a.spawned or false, hidden = a.hidden or false, ragdoll = a.ragdoll or false, has_ragdoll = E.component(a.go, "via.dynamics.Ragdoll") ~= nil, is_player = a.is_player, puppet = a.puppet, paused = a.paused, root_lock = a.root_lock ~= nil,
        pos = v3t(p), euler = { ex, ey, ez }, joints = a:joint_count(), layer_count = a:layer_count(), has_fsm = a.fsm ~= nil,
        layers = layers, banks = banks,
        lookat = { enabled = la.enabled or false, kind = la.kind or "camera", target = lookat_target and lookat_target.addr or nil, weight = la.weight or 1,
                   max_deg = la.max_deg or 75, smooth = la.smooth or 0.12, neck_share = la.neck_share or 0.35, flip = (la.forward_sign or 1) < 0 },
        pose = { joint = sel_joint, euler = jeuler, posed = posed, weight = w and w.layer and w.layer.weight or 1 },
        attached = (function() local at = X.attachment_of(a); if at then local op = at.off_pos; local ex, ey, ez = E.euler_deg(at.off_rot); return { actor = at.actor.addr, actor_name = at.actor.display_name or at.actor.name, joint = at.joint, pos = { op.x, op.y, op.z }, euler = { ex, ey, ez } } end end)(),
        scale = (function() local ok, s = pcall(function() return a.xform:call("get_LocalScale") end); return ok and s and s.x or 1 end)(),
        scale3 = (function() local ok, s = pcall(function() return a.xform:call("get_LocalScale") end); return ok and s and { s.x, s.y, s.z } or { 1, 1, 1 } end)(),
    }
end

local function seq_state()
    local s = Seq.current
    if not s then return { name = "", fps = 60, length = 600, loop = false, playing = false, t = 0, tracks = {} } end
    local tracks = {}
    for i, tr in ipairs(s.tracks) do
        local t = { idx = i, kind = tr.kind, name = tr.name, actor = tr.actor, actor_name = tr.actor_name, layer = tr.layer,
            missing = (tr.kind ~= "camera" and tr.kind ~= "cammove") and not (tr.actor and Actor.get(tr.actor)) or false }
        if tr.kind == "anim" then
            t.clips = {}
            for _, c in ipairs(tr.clips) do t.clips[#t.clips + 1] = { id = c.id, start = c.start, dur = c.dur, name = c.name, mot = c.mot, path = c.path, endframe = c.endframe, offset = c.offset, speed = c.speed, blend = c.blend, loop = c.loop, rm = c.rm ~= nil, rm_dist = c.rm_dist } end
        elseif tr.kind == "pose" then
            t.clips = {}
            for _, c in ipairs(tr.clips) do
                local kt, ke = {}, {}
                for i, k in ipairs(c.keys or {}) do kt[i] = k.t; ke[i] = k.ease or "smooth" end
                t.clips[#t.clips + 1] = { id = c.id, start = c.start, dur = c.dur, keys = kt, kease = ke, weight = c.weight, fade_in = c.fade_in, fade_out = c.fade_out, loop = c.loop, mode = c.mode }
            end
        elseif tr.kind == "xform" then
            t.keys = {}
            for _, k in ipairs(tr.keys) do
                local _, yaw = E.euler_deg(E.quat(k.rot[1], k.rot[2], k.rot[3], k.rot[4]))
                t.keys[#t.keys + 1] = { t = k.t, pos = k.pos, yaw = yaw, ease = k.ease }
            end
        elseif tr.kind == "cammove" then
            t.cam = tr.cam
            t.lookat_name = tr.lookat_name
            t.keys = {}
            for _, k in ipairs(tr.keys) do t.keys[#t.keys + 1] = { t = k.t, pos = k.pos, fov = k.fov, ease = k.ease, roll = k.roll } end
        elseif tr.kind == "camera" then
            t.cuts = {}
            for _, c in ipairs(tr.cuts) do t.cuts[#t.cuts + 1] = { id = c.id, start = c.start, cam = c.cam } end
        elseif tr.kind == "path" then
            t.start = tr.start; t.dur = tr.dur; t.speed = tr.speed; t.length = tr.length or 0; t.points = #(tr.points or {}); t.ease = tr.ease; t.gait = tr.gait; t.clip_name = tr.clip_name
            t.drawing = PathMod.drawing ~= nil and PathMod.drawing.track == tr; t.dur_pinned = tr.dur_pinned or false
        end
        tracks[#tracks + 1] = t
    end
    return { name = s.name, fps = s.fps, length = s.length, loop = s.loop, playing = Seq.playing, t = Seq.t, tracks = tracks, range = s.range, falloff = s.falloff, speed = Seq.speed, audio = s.audio, shots = s.shots or {},
             film_camera = Seq.film_camera(Seq.t) }
end

-- what the viewport looks through: "work" (free, never rendered) | "scene" (follows the film) | "camera" | "game"
local function camera_view()
    if Cam.work_on then return "work" end
    if Seq.scene_view then return "scene" end
    if Cam.enabled and Cam.active then return "camera" end
    return "game"
end

local function cameras_state()
    local out = {}
    for i, c in ipairs(Cam.cams) do
        local t = c.target and Actor.get(c.target)
        out[#out + 1] = { i = i, name = c.name, mode = c.mode, fov = c.fov, pos = c.pos, rot = c.rot, radius = c.radius, height = c.height, speed = c.speed,
            offset = c.offset, follow = c.follow, smooth = c.smooth, target = t and t.addr or nil, target_name = t and (t.display_name or t.name) or nil, live = (Cam.enabled and Cam.active == i) or false, dof = c.dof ~= false, dof_f = c.dof_f, dof_focus = c.dof_focus, roll = c.roll or 0, shake = c.shake }
    end
    return out
end

local function build_state()
    local actors = {}
    for _, a in ipairs(Actor.all()) do
        local ok, st = pcall(actor_state, a)
        if ok then actors[#actors + 1] = st end
    end
    local cam = E.primary_camera()
    local fov = cam and cam:call("get_FOV") or 0
    local tests = {}
    for k, v in pairs(Selftest.results) do tests[#tests + 1] = { name = k, ok = v.ok, detail = v.detail, time = v.time } end
    return {
        ack = B.last_cmd_id, data_ver = B.data_ver, clock = now(),
        actors = actors, cameras = cameras_state(), camera_override = Cam.enabled, active_camera = Cam.active, camera_view = camera_view(), camera_fly ={ on = Cam.fly.on, speed = Cam.fly.speed, sens = Cam.fly.sens }, grab = Grab.state(), game_focused = E.game_focused, edit = Edit.state(), autokey = B.autokey or false, drive = Drive.state_export(), hide_weapons = E.hide_weapons or false, hud_hidden = E.hud_hidden or false, history = History.state(), key_clipboard = B.key_clipboard and B.key_clipboard.count or 0, overlay = Cam.overlay, lights = Lights.describe(), skeleton = X.show_skeleton, onion = X.onion, motion_edit = X.motion_edit, stage_clean = X.stage_clean, stage = Stage.state(), constraints = Constraint.describe(), pick_props = B.pick_props or false, mix = B.mix, sound = B.sound_state, thumb = { on = B.thumb.on, stable = B.thumb.stable, name = B.mesh_preview_info and B.mesh_preview_info.name or nil }, sound_preview = B.preview_sound and { event = B.preview_sound.event } or nil, sound_preview_error = B.preview_sound_error, last_export = B.last_export, baking = RM.baking and { name = RM.baking.a.name, frame = RM.baking.last_f, n = RM.baking.n } or nil, auto_rm = B.auto_rm ~= false,
        selection = { actor = B.sel.actor, camera = B.sel.camera, clip = B.sel.clip, light = B.sel.light, multi = (function() local o = {}; for addr in pairs(B.sel.multi) do o[#o + 1] = addr end; return o end)() },
        sequence = seq_state(),
        gizmo = { enabled = Gizmo.enabled, target = Gizmo.target, op = Gizmo.op, mode = Gizmo.mode, snap = Gizmo.snap, cursor = reframework:is_drawing_ui() },
        game = { fov = fov, player_frozen = E.player_frozen, project = Project.current, frozen = E.frozen },
        render_clock = E.render_clock_state(),
        tests = tests, test_running = Selftest.running and Selftest.running.name or nil,
    }
end

-------------------------------------------------------------------------------
-- data (large, on change)
-------------------------------------------------------------------------------
local function build_data()
    local a_sel = cur_actor()
    local rig_code = a_sel and Import.rig_code(a_sel) or nil
    local scene = {}
    for _, c in ipairs(B.scan) do
        scene[#scene + 1] = { addr = c.go:get_address(), name = c.name, dist = c.dist, has_fsm = c.has_fsm, is_player = c.is_player, added = Actor.registry[c.go:get_address()] ~= nil }
    end
    local results = {}
    for _, e in ipairs(B.catalog.results) do results[#results + 1] = { i = e.i, p = e.p, n = e.n, c = e.c, g = e.g, bank = Catalog.bank_id(e.p) } end
    local motions, joints = {}, {}
    local a = cur_actor()
    if a then
        for bank_id, b in pairs(a.banks) do
            if b.ready and b.motions then
                local list = {}
                for _, m in ipairs(b.motions) do list[#list + 1] = { id = m.id, name = m.name, endframe = m.endframe } end
                motions[tostring(bank_id)] = list
            end
        end
        for bank_id, b in pairs(a.facial_banks or {}) do
            if b.ready and b.motions then
                local list = {}; for _, m in ipairs(b.motions) do list[#list + 1] = { id = m.id, name = m.name, endframe = m.endframe } end
                motions[tostring(bank_id)] = list
            end
        end
        joints = a:joint_names()
    end
    local objects = {}
    for _, c in ipairs(B.objects or {}) do
        objects[#objects + 1] = { addr = c.go:get_address(), name = c.name, dist = c.dist, added = Actor.registry[c.go:get_address()] ~= nil }
    end
    return {
        ver = B.data_ver, eval = B.eval, cast = Cast.load(), scene = scene, objects = objects, meshes = B.mesh_results or {},
        catalog = { query = B.catalog.query, group = B.catalog.group, owner = B.catalog.owner, results = results },
        groups = Catalog.groups, chars = Catalog.chars, owners = Catalog.owners, motions = motions, joints = joints,
        poses = Pose.list_files(), rig_code = rig_code, imports = rig_code and Import.list(rig_code) or {}, last_rig = B.last_rig, projects = Project.list(), log = Log.tail(60), stages = Stage.list(),
        mesh_preview = B.mesh_preview_info,
    }
end

-------------------------------------------------------------------------------
-- commands
-------------------------------------------------------------------------------
local ops = {}
B.sel.multi = {}
local function multi_list()
    local out = {}
    for addr in pairs(B.sel.multi) do local a = Actor.get(addr); if a and a:valid() then out[#out + 1] = a else B.sel.multi[addr] = nil end end
    return out
end
local function find_clip(track_idx, id)
    local s = Seq.current; if not s then return nil end
    local tr = s.tracks[track_idx]; if not tr or not tr.clips then return nil end
    for _, cl in ipairs(tr.clips) do if cl.id == id then return cl, tr end end
    return nil
end

ops.ping = function(c)
    B.editor_alive = now()
    if c.focused ~= nil and not E.force_focused then E.game_focused = c.focused and true or false end
end
-- unkeyed pose edits are dropped when the playhead moves (Blender behaviour without auto-key); keys drive the pose
local function drop_unkeyed_edits()
    local s = Seq.current; if not s then return end
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "pose" and tr.actor then
            local a = Actor.get(tr.actor)
            if a and a.work and a.work.layer then a.work.layer.joints = {}; a.work.saved_joints = {} end
        end
    end
end
Grab.ctx = function() return Gizmo.ui and Gizmo.ui() or nil end
-- edit mode plumbing
Edit.selected = function() return Actor.get(B.sel.actor) end
Edit.on_select = function(addr) local a = actor_by_addr(addr); if a then B.sel.actor = a.addr; B.data_dirty = true end end
Edit.candidates = function()
    local out, seen = {}, {}
    for _, a in ipairs(Actor.all()) do
        if a:valid() and a.kind ~= "object" then out[#out + 1] = { addr = a.addr, go = a.go, name = a.name, label = a.display_name or a.name }; seen[a.addr] = true end
    end
    for _, c in ipairs(B.scan) do
        local addr = c.go:get_address()
        if not seen[addr] and c.has_fsm then out[#out + 1] = { addr = addr, go = c.go, name = c.name, label = c.name } end
    end
    -- objects already in the level (chairs, barrels, lamps): clickable in the picture while "pick props" is on
    if B.pick_props then
        for _, a in ipairs(Actor.all()) do
            if a:valid() and a.kind == "object" and not seen[a.addr] then
                out[#out + 1] = { addr = a.addr, go = a.go, name = a.name, label = a.display_name or a.name, prop = true, radius = 0.35 }
                seen[a.addr] = true
            end
        end
        for _, c in ipairs(B.objects or {}) do
            local addr = c.go:get_address()
            if not seen[addr] then
                out[#out + 1] = { addr = addr, go = c.go, name = c.name, label = c.name, prop = true, radius = 0.35 }
                seen[addr] = true
            end
        end
    end
    return out
end
Edit.on_moved = function(a) if B.autokey then Seq.key_transform(a, math.floor(Seq.t)); for _, o in ipairs(multi_list()) do if o ~= a then Seq.key_transform(o, math.floor(Seq.t)) end end end end
Edit.group = function(a)
    if not B.sel.multi[a.addr] then return {} end
    local out = {}
    for _, o in ipairs(multi_list()) do if o ~= a then out[#out + 1] = o end end
    return out
end
Edit.on_place = function(place, pos, yaw)
    local a, err = Cast.spawn({ id = place.id, tree = place.tree, preset = place.preset, name = place.name, pos = pos, rot = E.quat_from_euler_deg(0, yaw, 0), no_idle = place.no_idle }, function() B.data_dirty = true end)
    if a then B.sel.actor = a.addr else Log.err("place: %s", tostring(err)) end
    B.data_dirty = true
end
ops.edit_mode = function(c) if c.value == nil then Edit.toggle() else Edit.enable(c.value and true or false) end end
-- IK: move a hand / foot to a world point (or by a delta); the limb follows, the effector keeps its orientation
ops.ik_solve = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not (a and c.chain and c.target) then return end
    IK.solve(a, c.chain, Vector3f.new(c.target[1] or c.target.x, c.target[2] or c.target.y, c.target[3] or c.target.z), { keep_effector = c.keep ~= false })
end
ops.ik_nudge = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not (a and c.chain and c.delta) then return end
    IK.nudge(a, c.chain, Vector3f.new(c.delta[1] or 0, c.delta[2] or 0, c.delta[3] or 0), { keep_effector = c.keep ~= false })
end
ops.ik_handles = function(c) Edit.ik_handles = c.value and true or false end
-- ---------------- paths: waypoints on the floor, the character walks the curve ----------------
local function path_pick_clip(a, gait)
    -- the character's own locomotion set (like drive mode); returns bank path + motion, or nil
    local Drive_general = (function()
        if a.cast and a.cast.cast and a.cast.cast.general then return a.cast.cast.general end
        for _, cc in ipairs(Cast.load()) do if cc.id == a.name:match("^(ch%w+)_") and cc.general then return cc.general end end
        local code = a.name:match("^ch%d([a-z]%w)") and ("ch" .. a.name:match("^ch%d([a-z]%w)")) or nil
        if code then for _, cc in ipairs(Cast.load()) do if cc.code == code and cc.general then return cc.general end end end
    end)()
    return Drive_general
end
local PATH_GAITS = { walk = { pats = { "walk_f_loop", "walk_front_loop", "walk_loop" }, speed = 1.35 }, jog = { pats = { "jog_loop_vera", "jog_loop", "run_loop", "jog_f_loop" }, speed = 3.1 } }
local function path_attach_clip(tr, a, gait)
    gait = PATH_GAITS[gait] and gait or "walk"
    local general = path_pick_clip(a, gait)
    if not general then Log.warn("path: no locomotion set for %s", a.name); return end
    a:load_motlist(general, function(bank, motions)
        local pick
        for _, pat in ipairs(PATH_GAITS[gait].pats) do
            for _, m in ipairs(motions) do local n = m.name:lower(); if n:find(pat, 1, true) and not n:find("stairs") and not n:find("curve") then pick = m; break end end
            if pick then break end
        end
        if not pick then Log.warn("path: no %s clip in %s", gait, general); return end
        local atr = Seq.track_for_actor(a, 0, true)
        -- replace a previous path clip
        if tr.clip_id then Seq.remove_clip(atr, tr.clip_id) end
        local clip = Seq.add_clip(atr, { start = tr.start, path = general, mot = pick.id, name = pick.name, endframe = pick.endframe, blend = 10, speed = 1, dur = tr.dur, loop = true })
        clip.path_owned = true
        tr.clip_id = clip.id; tr.clip_name = pick.name; tr.gait = gait
        tr.speed = tr.speed_pinned or PATH_GAITS[gait].speed
        tr.clip_speed_ref = tr.clip_speed_ref or PATH_GAITS[gait].speed
        Seq.path_refresh(tr)
        -- measure the clip's real travel in the background: feet then match the ground exactly
        B.rm_queue[#B.rm_queue + 1] = { track = atr, clip = clip, on_done = function(dist)
            if dist and clip.endframe and clip.endframe > 0 then
                local per_s = dist / (clip.endframe / 60)
                if per_s > 0.3 then tr.clip_speed_ref = per_s; if not tr.speed_pinned then tr.speed = per_s end; Seq.path_refresh(tr) end
            end
            clip.rm = nil; clip.base = nil -- the path owns the position
        end }
        B.data_dirty = true
    end)
end
-- start drawing a path for the selected character (P in edit mode does the same)
ops.path_new = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    B.sel.actor = a.addr
    local tr = Seq.path_track_for_actor(a, true)
    if c.clear ~= false and #tr.points == 0 then
        local p = a.xform:call("get_Position")
        tr.points[1] = { p.x, p.y, p.z } -- the path starts where she stands
    end
    tr.start = math.floor(c.start or tr.start or Seq.t)
    if not tr.clip_id then path_attach_clip(tr, a, c.gait or "walk") else Seq.path_refresh(tr) end
    PathMod.drawing = { track = tr }
    if not Edit.on then Edit.enable(true) end
    Log.info("path: drawing for %s (click the floor, Enter / P finishes)", a.name)
end
ops.path_point = function(c)
    local s = Seq.current; local tr = s and s.tracks[c.track]; if not (tr and tr.kind == "path") then return end
    local y = c.pos[2]; local gy = X.ground_y(c.pos[1], y, c.pos[3], 2, 4); if gy then y = gy end
    tr.points[#tr.points + 1] = { c.pos[1], y, c.pos[3] }
    Seq.path_refresh(tr)
end
ops.path_pop = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and tr.kind == "path" and #tr.points > 0 then table.remove(tr.points); Seq.path_refresh(tr) end end
ops.path_points = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and tr.kind == "path" then tr.points = c.points or {}; Seq.path_refresh(tr) end end
ops.path_update = function(c)
    local s = Seq.current; local tr = s and s.tracks[c.track]; if not (tr and tr.kind == "path") then return end
    local f = c.fields or {}
    if f.start ~= nil then tr.start = math.max(0, math.floor(f.start)) end
    if f.speed ~= nil then tr.speed = math.max(0.1, tonumber(f.speed) or 1.35); tr.speed_pinned = tr.speed; tr.dur_pinned = nil end
    if f.dur ~= nil then tr.dur = math.max(1, math.floor(f.dur)); tr.dur_pinned = true end
    if f.ease ~= nil then tr.ease = math.max(0, math.min(0.45, tonumber(f.ease) or 0.08)) end
    if f.reverse then local r = {}; for i = #tr.points, 1, -1 do r[#r + 1] = tr.points[i] end; tr.points = r end
    if f.gait then local a = Actor.get(tr.actor); if a then tr.speed_pinned = nil; path_attach_clip(tr, a, f.gait) end end
    Seq.path_refresh(tr)
end
ops.path_finish = function() PathMod.drawing = nil end
ops.path_remove = function(c)
    local s = Seq.current; local tr = s and s.tracks[c.track]; if not (tr and tr.kind == "path") then return end
    local a = Actor.get(tr.actor)
    if a and tr.clip_id then local atr = Seq.track_for_actor(a, 0, false); if atr then Seq.remove_clip(atr, tr.clip_id) end end
    for i, x in ipairs(s.tracks) do if x == tr then table.remove(s.tracks, i); break end end
    if PathMod.drawing and PathMod.drawing.track == tr then PathMod.drawing = nil end
end
-- edit mode plumbing for the path pen
Edit.on_path_point = function(hit)
    local d = PathMod.drawing; if not d then return end
    local s = Seq.current; if not s then return end
    for i, tr in ipairs(s.tracks) do if tr == d.track then ops.path_point({ track = i, pos = { hit.x, hit.y, hit.z } }); return end end
end
Edit.on_path_pop = function()
    local d = PathMod.drawing; if not d then return end
    local s = Seq.current; if not s then return end
    for i, tr in ipairs(s.tracks) do if tr == d.track then ops.path_pop({ track = i }); return end end
end
Edit.on_path_toggle = function()
    if PathMod.drawing then PathMod.drawing = nil else ops.path_new({}) end
end
Edit.path_drawing = function() return PathMod.drawing ~= nil end
-- the bake queue can call back
E.on_frame(function() end)
-- external animations: rig capture for the retargeter, and laying a retargeted clip onto the timeline
ops.export_rig = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    B.last_rig = Import.export_rig(a)
    B.data_dirty = true
end
ops.import_anim = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not (a and c.file) then return end
    local clip, err = Import.apply(a, c.file, c.start or Seq.t, { loop = c.loop, root = c.root, root_every = c.root_every })
    if not clip then Log.err("import: %s", tostring(err)) else B.sel.clip = nil end
    if not Seq.playing then Seq.evaluate(Seq.t, false) end
    B.data_dirty = true
end
-- lights: point / spot. New lights appear 1.5 m in front of the live view (or above the selected character) and aim at it
ops.light_add = function(c)
    local pose = view_pose()
    local a = Actor.get(B.sel.actor)
    local pos, rot
    if a and a:valid() and c.above ~= false then
        local p = a.xform:call("get_Position")
        local f = pose and E.basis(E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])) or Vector3f.new(0, 0, 1)
        pos = Vector3f.new(p.x - f.x * 1.2, p.y + 2.2, p.z - f.z * 1.2)
        rot = E.look_rotation(pos, Vector3f.new(p.x, p.y + 1.3, p.z))
    elseif pose then
        local f = E.basis(E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4]))
        pos = Vector3f.new(pose.pos[1] + f.x * 1.5, pose.pos[2] + f.y * 1.5, pose.pos[3] + f.z * 1.5)
        rot = E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])
    end
    local l, err = Lights.add(c.kind or "spot", pos, rot, { name = c.name })
    if l then B.sel.light = l.id else Log.err("light: %s", tostring(err)) end
end
ops.light_update = function(c) Lights.update(c.id or B.sel.light, c.fields) end
ops.light_remove = function(c) Lights.remove(c.id or B.sel.light); if B.sel.light == (c.id or B.sel.light) then B.sel.light = nil end end
ops.light_select = function(c) B.sel.light = c.id end
ops.light_aim = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local p = a.xform:call("get_Position")
    Lights.aim(c.id or B.sel.light, Vector3f.new(p.x, p.y + (c.height or 1.3), p.z))
end
ops.light_to_camera = function(c)
    local pose = view_pose(); if not pose then return end
    Lights.update(c.id or B.sel.light, { pos = pose.pos })
    local l = Lights.get(c.id or B.sel.light); if l then l.xf:call("set_Rotation", E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])) end
end
Edit.on_ik_done = function(a, chain)
    if not B.autokey then return end
    local w = a.work
    if not (w and w.layer) then return end
    local j = {}
    for _, n in ipairs(IK.CHAINS[chain] or {}) do if w.layer.joints[n] then j[n] = w.layer.joints[n] end end
    if next(j) then X.commit_pose_edit(a, j, w.layer) end
end
ops.cast_place = function(c) Edit.place = { id = c.id, tree = c.tree, preset = c.preset, name = c.name, no_idle = c.no_idle }; if not Edit.on then Edit.enable(true) end end
ops.autokey = function(c) B.autokey = c.value and true or false end
ops.test_focus = function(c) E.force_focused = c.value and true or false; if c.value then E.game_focused = true end end -- tests only
-- auto-key from in-picture edits
Gizmo.on_edit_done = function(ed)
    if not B.autokey then return end
    if ed.kind == "actor" then Seq.key_transform(ed.actor, math.floor(Seq.t))
    elseif ed.kind == "bone" and ed.layer and ed.layer.joints[ed.joint] then X.commit_pose_edit(ed.actor, { [ed.joint] = ed.layer.joints[ed.joint] }, ed.layer) end
end
Grab.on_confirm = function(g)
    if not B.autokey then return end
    if g.kind == "bone" and g.layer and g.layer.joints[g.name] then X.commit_pose_edit(g.actor, { [g.name] = g.layer.joints[g.name] }, g.layer)
    elseif g.actor then Seq.key_transform(g.actor, math.floor(Seq.t)) end
end
-- K / I: key whatever is selected (bone edits -> pose key, else the character's placement; camera when a camera is selected)
ops.key_selection = function(c)
    local t = math.floor(c.t or Seq.t)
    local a = Actor.get(B.sel.actor)
    if Gizmo.target == "camera" or (not a and (B.sel.camera or 0) > 0) then
        local i = Cam.active or B.sel.camera
        if i and Cam.cams[i] then Seq.key_camera(i, t) end
        return
    end
    if not a then return end
    local w = a.work
    if Gizmo.target == "bone" and w and w.layer and next(w.layer.joints) then
        Seq.keyframe(a, t, w.layer.joints)
    else
        Seq.key_transform(a, t)
    end
end
-- key editing
ops.move_key = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and (tr.kind == "xform" or tr.kind == "cammove") then Seq.move_key(tr, c.t, c.new_t) end end
-- drive & record
ops.drive = function(c)
    if c.value == false then Drive.stop(); return end
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    B.sel.actor = a.addr
    local ok, err = Drive.start(a)
    if not ok then Log.err("drive: %s", tostring(err)) end
end
ops.record = function(c) if Drive.on then Drive.set_recording(c.value and true or false) else Log.warn("record: drive a character first") end end
Cam.on_key_camera = function(i) local t = math.floor(Seq.t); if Seq.key_camera(i, t) then B.data_dirty = true; Log.info("camera %d keyed @ %d", i, t) end end
ops.refresh = function() refresh_scan(true) end

-- selection / actors
-- Shift+click adds / removes; a plain click selects one (and clears the group)
ops.select_actor = function(c)
    local a = c.addr and actor_by_addr(c.addr)
    if c.add and a then
        if B.sel.actor and B.sel.actor ~= a.addr then B.sel.multi[B.sel.actor] = true end
        if B.sel.multi[a.addr] and B.sel.actor ~= a.addr then B.sel.multi[a.addr] = nil else B.sel.multi[a.addr] = true; B.sel.actor = a.addr end
    else
        B.sel.multi = {}
        B.sel.actor = a and a.addr or nil
    end
    B.data_dirty = true
end
ops.select_clear = function() B.sel.multi = {}; B.sel.actor = nil; B.data_dirty = true end
-- group operations on the whole selection
ops.group_key = function(c) local t = math.floor(c.t or Seq.t); for _, a in ipairs(multi_list()) do Seq.key_transform(a, t) end end
ops.group_release = function() for _, a in ipairs(multi_list()) do a:release() end; B.sel.multi = {} end
ops.group_remove = function() for _, a in ipairs(multi_list()) do if a.cast then Cast.destroy(a) elseif a.spawned then pcall(function() a.go:call("destroy", a.go) end); a:forget() end end; B.sel.multi = {}; B.sel.actor = nil; B.data_dirty = true end
-- duplicate a spawned cast member together with its animation / pose / position tracks
ops.duplicate_cast = function(c)
    local src = Actor.get(c.addr or B.sel.actor); if not (src and src.cast) then return end
    local p = src.xform:call("get_Position"); local r = src.xform:call("get_Rotation")
    local pos = Vector3f.new(p.x + (c.dx or 0.8), p.y, p.z + (c.dz or 0))
    local a = Cast.spawn({ id = src.cast.cast.id, tree = src.cast.cast.tree, preset = src.cast.preset_name or src.cast.preset, name = (src.display_name or src.cast.name or "copy") .. " copy", pos = pos, rot = r, no_idle = true }, function(na)
        local s = Seq.current; if not s then return end
        local copies = {}
        for _, tr in ipairs(s.tracks) do
            if tr.actor == src.addr then
                if tr.kind == "anim" then
                    local nt = Seq.add_anim_track(na, tr.layer)
                    for _, cl in ipairs(tr.clips) do local n = {}; for k, v in pairs(cl) do n[k] = v end; n.id = nil; n.rm = nil; n.base = nil; Seq.add_clip(nt, n) end
                    for _, cl in ipairs(nt.clips) do na:load_motlist(cl.path) end
                elseif tr.kind == "pose" then
                    local nt = Seq.add_pose_track(na)
                    for _, cl in ipairs(tr.clips) do
                        local keys = {}
                        for _, k in ipairs(cl.keys or {}) do local j = {}; for n2, v in pairs(k.joints) do j[n2] = { v[1], v[2], v[3], v[4] } end; keys[#keys + 1] = { t = k.t, joints = j, ease = k.ease } end
                        Seq.add_pose_clip(nt, { start = cl.start, dur = cl.dur, keys = keys, weight = cl.weight, fade_in = cl.fade_in, fade_out = cl.fade_out, loop = cl.loop, mode = cl.mode })
                    end
                elseif tr.kind == "xform" then
                    for _, k in ipairs(tr.keys) do
                        local _, key = Seq.key_transform(na, k.t, Vector3f.new(k.pos[1] + (pos.x - p.x), k.pos[2], k.pos[3] + (pos.z - p.z)), E.quat(k.rot[1], k.rot[2], k.rot[3], k.rot[4]))
                        key.ease = k.ease
                    end
                end
                copies[#copies + 1] = tr.kind
            end
        end
        Log.info("duplicated %s with %d track(s)", src.name, #copies)
        B.data_dirty = true
    end)
    if a then B.sel.actor = a.addr end
    B.data_dirty = true
end
ops.add_actor = function(c) local a = c.addr and actor_by_addr(c.addr); if a then B.sel.actor = a.addr end; B.data_dirty = true end
ops.forget_actor = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:forget(); if B.sel.actor == a.addr then B.sel.actor = nil end end; B.data_dirty = true end
ops.release_actor = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:release() end end
ops.release_all = function() Seq.release(); for _, a in ipairs(Actor.all()) do a:release() end; Cam.stop(); E.set_player_frozen(false) end
ops.set_puppet = function(c) local a = c.addr and actor_by_addr(c.addr) or Actor.get(B.sel.actor); if a then a:set_puppet(c.value and true or false) end; B.data_dirty = true end
ops.game_freeze = function(c) E.set_frozen(c.value and true or false) end
ops.want_objects = function(c) B.want_objects = c.value and true or false; if B.want_objects then refresh_scan(true) end end
ops.cam_dof = function(c) local cam = Cam.cams[c.i or B.sel.camera]; if cam then cam.dof = c.value and true or false end end
-- cinematic DoF: f = aperture (1.4 shallow .. 16 deep), focus = "target" (auto-focus on the look-at target) | metres | nil (game)
ops.cam_dof_params = function(c)
    local cam = Cam.cams[c.i or B.sel.camera]; if not cam then return end
    if c.f ~= nil then cam.dof_f = (c.f ~= false) and tonumber(c.f) or nil end
    if c.focus ~= nil then cam.dof_focus = (c.focus == false) and nil or c.focus end
    if c.f ~= nil or c.focus ~= nil then cam.dof = true; cam.auto = nil end
end
local function selection_center()
    local ids = {}
    if B.sel.actor then ids[#ids + 1] = B.sel.actor end
    for id in pairs(B.sel.multi or {}) do
        local found = false; for _, x in ipairs(ids) do if x == id then found = true end end
        if not found then ids[#ids + 1] = id end
    end
    local sum, n = Vector3f.new(0, 0, 0), 0
    for _, id in ipairs(ids) do
        local a = Actor.get(id)
        if a and a:valid() then
            local p = E.v3(a.xform:call("get_Position")); if a.kind ~= "object" then p.y = p.y + 1.25 end
            sum = sum + p; n = n + 1
        end
    end
    return n > 0 and Vector3f.new(sum.x / n, sum.y / n, sum.z / n) or nil
end
ops.cam_focus_selection = function(c)
    local i = c.i or B.sel.camera; local cam = Cam.cams[i or 0]; local point = selection_center()
    if not (cam and point) then return end
    local eye = Cam.evaluate(cam, 0); if not eye then return end
    cam.dof = true; cam.dof_f = cam.dof_f or 2.8; cam.dof_focus = (point - eye):length(); cam.auto = nil
    if c.key then Seq.key_camera(i, math.floor(c.t or Seq.t)) end
end
ops.cam_target_selection = function(c)
    local cam = Cam.cams[c.i or B.sel.camera or 0]
    local a = Actor.get(c.addr or B.sel.actor)
    if cam and a then Cam.set_target(cam, a.addr, tonumber(c.duration) or 1.0) end
end
-- game HUD on/off (render): via.gui.GUIManager master switch; falls back to the chainsaw GUI master if present
-- game HUD on/off (render): every via.gui.GUI component in the scene is switched off and remembered; the game re-enables
-- some of them by itself (reticle, float icons), so the hide is re-applied every frame while it is on
B.hud_hidden_comps = nil
local function hud_apply(hide)
    local scene = sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene")
    local comps = scene:call("findComponents(System.Type)", sdk.typeof("via.gui.GUI"))
    if not comps then return 0 end
    local n = comps:get_size()
    if hide then
        B.hud_hidden_comps = B.hud_hidden_comps or {}
        for i = 0, n - 1 do
            local c = comps:get_element(i)
            if c and c:call("get_Enabled") then c:call("set_Enabled", false); B.hud_hidden_comps[#B.hud_hidden_comps + 1] = c end
        end
        return #B.hud_hidden_comps
    else
        local k = 0
        for _, c in ipairs(B.hud_hidden_comps or {}) do if E.valid(c) then pcall(function() c:call("set_Enabled", true) end); k = k + 1 end end
        B.hud_hidden_comps = nil
        return k
    end
end
ops.hud = function(c)
    local on = c.value and true or false
    E.hud_hidden = not on
    local ok, n = pcall(hud_apply, not on)
    Log.info("hud %s (%s)", on and "on" or "off", ok and (tostring(n) .. " gui") or ("error: " .. tostring(n)))
end
E.on_frame(function() if E.hud_hidden then pcall(hud_apply, true) end end)
-- the player's weapons (wp* objects under the body) drawn or not: a preaching Leon holds no handgun
ops.hide_weapons = function(c) E.hide_weapons = c.value and true or false; E.weapons_dirty = true end
E.on_frame(function()
    if not (E.hide_weapons or E.weapons_dirty) then return end
    local pb = E.player_body(); if not pb then return end
    local ok = pcall(function()
        local xf = pb:call("get_Transform")
        for i = 0, (xf:call("get_ChildCount") or 0) - 1 do
            local ch = xf:call("getChild", i); local go = ch and ch:call("get_GameObject")
            if go and (go:call("get_Name") or ""):match("^wp%d") then go:call("set_DrawSelf", not E.hide_weapons) end
        end
    end)
    if ok and not E.hide_weapons then E.weapons_dirty = false end
end)
ops.show_cameras = function(c) Gizmo.show_cameras = c.value and true or false end

-- mesh catalog (offline JSON built by tools/scripts/build_catalog.py) and spawning
local mesh_catalog = nil
local function load_meshes()
    if mesh_catalog then return mesh_catalog end
    local d = json.load_file("director/catalog/meshes.json")
    mesh_catalog = (d and d.meshes) or {}
    return mesh_catalog
end
ops.mesh_search = function(c)
    local q = (c.q or ""):lower()
    local words = {}
    for w in q:gmatch("%S+") do words[#words + 1] = w end
    local out = {}
    for _, m in ipairs(load_meshes()) do
        local hay = (m.p .. " " .. (m.c or "")):lower()
        local ok = true
        for _, w in ipairs(words) do if not hay:find(w, 1, true) then ok = false; break end end
        if ok and (not c.cat or c.cat == "" or m.c == c.cat) then
            out[#out + 1] = m
            if #out >= (c.limit or 120) then break end
        end
    end
    B.mesh_results = out
    B.data_dirty = true
end
B.spawned = {}
local mesh_preview = nil
local mesh_preview_serial = 0
-- the hovered prop hangs in front of the camera and turns slowly, like a museum case:
-- ANCHOR is where it sits in camera space, FIT is how big it is drawn whatever its real size.
local PREVIEW_ANCHOR = Vector3f.new(0.62, -0.30, -1.7)
local PREVIEW_FIT = 0.45
-- Catalogue mode (tools/scripts/prop_thumbs.py): the prop is centred, still and larger, so a screenshot of the
-- picture is a usable thumbnail. B.thumb.stable counts frames since the fit converged — the capture script
-- waits for it instead of guessing a delay.
B.thumb = { on = false, fit = 0.85, anchor = Vector3f.new(0, 0, -2.2), yaw = 35, pitch = -14, stable = 0 }
B.mesh_preview_info = nil
local mesh_preview_fit = nil   -- { center = Vector3f, scale = number }
local function clear_mesh_preview()
    mesh_preview_serial = mesh_preview_serial + 1
    mesh_preview_fit = nil
    B.thumb.stable = 0
    B.preview_name = nil
    B.mesh_preview_info = nil
    if mesh_preview then
        pcall(function() mesh_preview:call("get_Transform"):call("set_Parent", nil) end)
        pcall(function() mesh_preview:call("destroy", mesh_preview) end)
        mesh_preview = nil
    end
end
local function asset_path(path)
    if not path then return nil end
    -- The extracted file list is lowercase, while archive resource lookups
    -- preserve the game's spelling (notably Environment and _Mat.mdf2).
    local canonical = path:gsub("^_chainsaw/", "_Chainsaw/")
        :gsub("^_Chainsaw/character/", "_Chainsaw/Character/")
        :gsub("^_Chainsaw/environment/", "_Chainsaw/Environment/")
        :gsub("^_Chainsaw/appsystem/", "_Chainsaw/AppSystem/")
        :gsub("^_Chainsaw/animation/", "_Chainsaw/Animation/")
        :gsub("/sm(%d)x/", function(d) return "/sm" .. d .. "X/" end)
        :gsub("_mat%.mdf2$", "_Mat.mdf2")
    return canonical
end
local function create_mesh(c, preview)
    if not c.mesh then return end
    if preview then clear_mesh_preview() end
    local pose = view_pose()
    local camera = E.primary_camera()
    local camera_go = camera and camera:call("get_GameObject")
    local camera_xf = camera_go and camera_go:call("get_Transform")
    local folder = camera_go and camera_go:call("get_Folder")
    if not (camera_xf and folder) then Log.err("spawn: no active camera scene"); return end
    -- A bare GameObject.create(name) has an inert world matrix in RE4. Give the
    -- mesh an active scene folder and parent it to the updating camera first.
    -- Placed props are detached once their world transform has initialized.
    local go = sdk.call_native_func(nil, sdk.find_type_definition("via.GameObject"), "create(System.String, via.Folder)",
        preview and "Director_Preview" or (c.name or ("Prop_" .. tostring(#B.spawned + 1))), folder)
    if not go then Log.err("spawn: GameObject.create failed"); return end
    go = go:add_ref()
    local comp = go:call("createComponent(System.Type)", sdk.typeof("via.render.Mesh"))
    local mres = sdk.create_resource("via.render.MeshResource", asset_path(c.mesh))
    if not mres then Log.err("spawn: mesh not found %s", c.mesh); pcall(function() go:call("destroy", go) end); return end
    comp:call("setMesh", mres:add_ref():create_holder("via.render.MeshResourceHolder"))
    if c.mdf then
        local mdf = sdk.create_resource("via.render.MeshMaterialResource", asset_path(c.mdf))
        if mdf then comp:call("set_Material", mdf:add_ref():create_holder("via.render.MeshMaterialResourceHolder")) end
    end
    local xf = go:call("get_Transform")
    xf:call("set_Parent", camera_xf)
    if preview then
        xf:call("set_LocalPosition", PREVIEW_ANCHOR)
        xf:call("set_LocalScale", Vector3f.new(0.0001, 0.0001, 0.0001)) -- invisible until it has been measured
    else
        xf:call("set_LocalPosition", Vector3f.new(0, 0, -2))
    end
    local target_pos
    if c.pos then
        target_pos = Vector3f.new(c.pos[1], c.pos[2], c.pos[3])
    elseif pose then
        local rot = E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])
        local f = E.basis(rot)
        local p = Vector3f.new(pose.pos[1] + f.x * 2.5, pose.pos[2] - 0.65, pose.pos[3] + f.z * 2.5)
        if not preview then
            local body = E.player_body()
            if body then p = X.resolve_motion(E.v3(body:call("get_Transform"):call("get_Position")), p, 0.35) or p end
            local gy = X.ground_y(p.x, p.y, p.z, 3, 8)
            if gy then p.y = gy end
        end
        target_pos = p
    end
    if preview then
        mesh_preview = go
        B.preview_name = c.name
        local serial = mesh_preview_serial
        local function frame_preview(tries)
            if serial ~= mesh_preview_serial or not E.valid(go) then return end
            if comp:call("get_MeshReady") and (not c.mdf or comp:call("get_MaterialReady")) then
                mesh_preview_fit = { name = c.name }   -- the per-frame rig below sizes and centres it
            elseif tries < 90 then E.defer(function() frame_preview(tries + 1) end, 1)
            else Log.warn("preview resource not ready: %s", c.mesh) end
        end
        E.defer(function() frame_preview(0) end, 2)
        return go
    end
    E.defer(function()
        if not E.valid(go) then return end
        xf:call("set_Parent", nil)
        if target_pos then xf:call("set_Position", target_pos) end
        xf:call("set_Rotation", c.rot and E.quat(c.rot[1], c.rot[2], c.rot[3], c.rot[4]) or E.quat_identity())
        if c.scale then xf:call("set_LocalScale", Vector3f.new(c.scale[1], c.scale[2], c.scale[3])) end
        B.data_dirty = true
    end, 3)
    B.spawned[#B.spawned + 1] = go
    local a = Actor.wrap(go)
    if a then
        a.spawned = true
        a.mesh_asset = { path = c.mesh, mdf = c.mdf }
        if not c.from_project then B.sel.actor = a.addr end
    end
    B.want_objects = true
    refresh_scan(true)
    Log.info("spawned %s", c.mesh)
    return a
end
Project.spawn_mesh = function(c) c.from_project = true; return create_mesh(c, false) end
ops.spawn_mesh = function(c) clear_mesh_preview(); create_mesh(c, false) end
ops.mesh_preview = function(c) create_mesh(c, true) end
-- catalogue capture: a still camera pointed at empty sky, no HUD, the prop centred in the picture
ops.thumb_stage = function(c)
    local on = c.value ~= false
    B.thumb.on = on
    B.thumb.stable = 0
    clear_mesh_preview()
    if on then
        local pb = E.player_body()
        local p = pb and pb:call("get_Transform"):call("get_Position")
        local i = Cam.capture("Director thumbnails")
        if i then
            local cam = Cam.cams[i]
            cam.mode = "static"
            cam.fov = 40
            cam.dof = false            -- a catalogue picture must be sharp
            cam.shake = nil; cam.roll = 0
            -- high above the level: nothing but sky behind every prop, wherever the player happens to be
            if p then cam.pos = { p.x, p.y + (c.height or 70), p.z } end
            -- look up at the sky, so every prop is photographed against the same empty background
            local q = E.quat_from_euler_deg(c.pitch or 55, c.yaw or 0, 0)
            cam.rot = { q.w, q.x, q.y, q.z }
            Cam.set_active(i)
            B.thumb.cam = i
        end
        -- two soft lights ride with the camera so props are photographed, not silhouetted
        local Lights = require("Director.lights")
        B.thumb.lights = {}
        local cam = Cam.cams[B.thumb.cam]
        if cam then
            local q2 = E.quat(cam.rot[1], cam.rot[2], cam.rot[3], cam.rot[4])
            local f, r, u = E.basis(q2)
            for _, L in ipairs({ { 1.6, -1.1, 0.8, 900 }, { 1.6, 1.1, -0.5, 350 } }) do
                local l = Lights.add({ kind = "point" })
                if l then
                    Lights.update(l.id, { pos = { cam.pos[1] + f.x * L[1] + r.x * L[2] + u.x * L[3],
                                                  cam.pos[2] + f.y * L[1] + r.y * L[2] + u.y * L[3],
                                                  cam.pos[3] + f.z * L[1] + r.z * L[2] + u.z * L[3] },
                        intensity = L[4], radius = 6, color = { 1, 0.97, 0.93 }, shadows = false, enabled = true })
                    B.thumb.lights[#B.thumb.lights + 1] = l.id
                end
            end
            B.sel.light = nil
        end
        ops.hud({ value = false })
        if c.freeze then E.set_frozen(true) end
    else
        if E.frozen then E.set_frozen(false) end
        local Lights = require("Director.lights")
        for _, id in ipairs(B.thumb.lights or {}) do pcall(Lights.remove, id) end
        B.thumb.lights = nil
        ops.hud({ value = true })
        if B.thumb.cam then
            Cam.stop()
            if Cam.cams[B.thumb.cam] then Cam.remove(B.thumb.cam) end
            B.thumb.cam = nil
        end
    end
    B.data_dirty = true
end
ops.mesh_preview_clear = function() clear_mesh_preview() end
ops.destroy_object = function(c)
    local a = Actor.get(c.addr or B.sel.actor)
    if a and a.spawned then
        if a.cast then Cast.destroy(a) else
            pcall(function() a.xform:call("set_Parent", nil) end)
            pcall(function() a.go:call("destroy", a.go) end)
            a:forget()
        end
        if B.sel.actor == a.addr then B.sel.actor = nil end
        B.data_dirty = true
    end
end
-- spawn a cast member in front of the camera, standing at the player's floor height, facing the camera
B.cast_preview = nil
local function clear_cast_preview()
    if B.cast_preview then pcall(Cast.destroy, B.cast_preview); B.cast_preview = nil; B.data_dirty = true end
end
local function cast_spawn_pose(distance)
    local pose = view_pose(); if not pose then return nil, nil end
    local q = E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4])
    local f = E.basis(q); f.y = 0; if f:length() < 0.01 then f = Vector3f.new(0, 0, 1) end; f = f:normalized()
    local floor_y = pose.pos[2] - 1.6; local pb = E.player_body(); if pb then floor_y = pb:call("get_Transform"):call("get_Position").y end
    local pos = Vector3f.new(pose.pos[1] + f.x * distance, floor_y, pose.pos[3] + f.z * distance)
    if pb then pos = X.resolve_motion(E.v3(pb:call("get_Transform"):call("get_Position")), pos, 0.45) or pos end
    local gy = X.ground_y(pos.x, floor_y, pos.z, 5, 12); if gy then pos.y = gy end
    return pos, E.quat_from_euler_deg(0, math.deg(math.atan(-f.x, -f.z)), 0)
end
ops.cast_preview_clear = function() clear_cast_preview() end
ops.cast_preview = function(c)
    clear_cast_preview()
    local pos, rot = cast_spawn_pose(c.distance or 2.5)
    local a = Cast.spawn({ id = c.id, tree = c.tree, preset = c.preset, name = "Preview", pos = pos, rot = rot, no_idle = true })
    if a then a.preview = true; a.internal = true; B.cast_preview = a end
end
ops.spawn_cast = function(c)
    clear_cast_preview()
    local pos, rot = cast_spawn_pose(c.distance or 2.5)
    local a, err = Cast.spawn({ id = c.id, tree = c.tree, preset = c.preset, name = c.name, pos = pos, rot = rot, no_idle = c.no_idle and true or false }, function()
        B.data_dirty = true
    end)
    if not a then Log.err("spawn_cast: %s", tostring(err)); return end
    B.sel.actor = a.addr
    B.data_dirty = true
end
ops.cast_gore = function(c) local a = Actor.get(c.addr or B.sel.actor); if a and a.cast then Cast.set_gore(a, c.value) end end
ops.cast_physics = function(c) local a = Actor.get(c.addr or B.sel.actor); if a and a.cast then Cast.set_physics(a, c.value and true or false); B.data_dirty = true end end
ops.cast_preset = function(c)
    local a = Actor.get(c.addr or B.sel.actor)
    if a and a.cast and c.preset ~= nil then Cast.set_preset(a, c.preset); B.data_dirty = true end
end
ops.destroy_cast_all = function() Cast.destroy_all(); B.sel.actor = nil; B.data_dirty = true end
ops.set_root_lock = function(c) local a = c.addr and actor_by_addr(c.addr) or Actor.get(B.sel.actor); if a then a:set_root_lock(c.value and true or false) end end
ops.set_paused = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:set_paused(c.value and true or false) end end
ops.set_transform = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    if B.autokey then E.defer(function() Seq.key_transform(a, math.floor(Seq.t)) end, 1) end
    local pos = c.pos and Vector3f.new(c.pos[1], c.pos[2], c.pos[3]) or nil
    if pos and a.kind ~= "object" then
        local cur = E.v3(a.xform:call("get_Position")); pos = X.resolve_motion(cur, pos) or pos
        local gy = X.ground_y(pos.x, pos.y, pos.z, 2, 5); if gy and math.abs(gy - pos.y) < 2.5 then pos.y = gy end
    end
    local rot = nil
    if c.euler then rot = E.quat_from_euler_deg(c.euler[1], c.euler[2], c.euler[3]) end
    if not a.root_lock then a:set_root_lock(true) end
    a:set_root_pose(pos, rot)
end
ops.set_scale = function(c) local a = Actor.get(c.addr or B.sel.actor); if a and c.scale then pcall(function() a.xform:call("set_LocalScale", Vector3f.new(c.scale[1], c.scale[2], c.scale[3])) end) end end
ops.move_to_camera = function(c)
    local a = Actor.get(c.addr or B.sel.actor); local pose = view_pose()
    if a and pose then local p = E.v3(a.xform:call("get_Position")); if not a.root_lock then a:set_root_lock(true) end; a:set_root_pose(Vector3f.new(pose.pos[1], p.y, pose.pos[3]), nil) end
end
ops.face_camera = function(c)
    local a = Actor.get(c.addr or B.sel.actor); local pose = view_pose()
    if not (a and pose) then return end
    local p = E.v3(a.xform:call("get_Position"))
    local d = Vector3f.new(pose.pos[1] - p.x, 0, pose.pos[3] - p.z)
    if d:length() < 0.01 then return end
    local fwd = E.v3(a.xform:call("get_AxisZ")); fwd.y = 0
    if not a.root_lock then a:set_root_lock(true) end
    a:set_root_pose(nil, (Lookat.rot_between(fwd, d) * a.xform:call("get_Rotation")):normalized())
end
ops.flip_facing = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:set_root_pose(nil, (E.quat_from_euler_deg(0, 180, 0) * a.xform:call("get_Rotation")):normalized()) end end
ops.key_transform = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local pos = c.pos and Vector3f.new(c.pos[1], c.pos[2], c.pos[3]) or (a.root_lock and a.root_lock.pos) or nil
    local rot = c.euler and E.quat_from_euler_deg(c.euler[1], c.euler[2], c.euler[3]) or (a.root_lock and a.root_lock.rot) or nil
    Seq.key_transform(a, math.floor(c.t or Seq.t), pos, rot)
end

-- catalog / motlists / playback
ops.catalog_search = function(c)
    B.catalog.query = c.q or ""; B.catalog.group = (c.group and c.group ~= "") and c.group or nil
    B.catalog.owner = (c.owner and c.owner ~= "") and c.owner or nil
    B.catalog.results = Catalog.search(B.catalog.query, B.catalog.group, B.catalog.owner, c.limit or 240)
    B.data_dirty = true
end
ops.load_motlist = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a or not c.path then return end
    a:load_motlist(c.path, function() B.data_dirty = true end)
    B.data_dirty = true
end
ops.play = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    a:play(c.bank, c.mot, { layer = c.layer or 0, blend = c.blend or 10, speed = c.speed or 1 })
end
ops.set_speed = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:set_speed(c.layer or 0, c.value or 1) end end
ops.set_frame = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then a:set_frame(c.layer or 0, c.value or 0) end end

-- sequence
ops.seq_play = function() drop_unkeyed_edits(); Seq.play() end
ops.seq_pause = function() Seq.pause() end
ops.seq_toggle = function() if not Seq.playing then drop_unkeyed_edits() end; Seq.toggle() end
ops.seq_stop = function() drop_unkeyed_edits(); Seq.stop() end
ops.seq_seek = function(c) Seq.pause(); drop_unkeyed_edits(); Seq.seek(c.t or 0) end
ops.render_clock_begin = function(c)
    clear_mesh_preview()  -- a hovered prop must never be filmed
    Seq.pause()
    local ok, err = E.render_clock_begin(c.fps or 30)
    if not ok then Log.err("render clock begin: %s", tostring(err)) end
end
ops.render_clock_step = function(c)
    Seq.pause()
    Seq.seek(c.t or Seq.t)
    local ok, err = E.render_clock_step(c.token, c.seconds)
    if not ok then Log.err("render clock step: %s", tostring(err)) end
end
ops.render_clock_end = function() E.render_clock_end() end
-- clip editing
ops.split_clip = function(c) local cl, tr = find_clip(c.track, c.id); if cl then Seq.split_clip(tr, cl, c.t or Seq.t) end end
ops.trim_clip = function(c) local cl, tr = find_clip(c.track, c.id); if cl then Seq.trim_clip(tr, cl, c.t or Seq.t, c.side or "left") end end
ops.paste_clip = function(c) local cl, tr = find_clip(c.track, c.id); if cl then local n = Seq.paste_clip(tr, cl, c.t or Seq.t); if n then B.sel.clip = { track = c.track, id = n.id, kind = tr.kind } end end end
ops.duplicate_clip = function(c) local cl, tr = find_clip(c.track, c.id); if cl then local n = Seq.duplicate_clip(tr, cl); if n then B.sel.clip = { track = c.track, id = n.id, kind = tr.kind } end end end
-- camera move keys
ops.key_camera = function(c)
    -- the live scene camera, else the selected one (the work camera is never keyed)
    local i = c.i or ((Cam.active or 0) > 0 and Cam.active) or ((B.sel.camera or 0) > 0 and B.sel.camera or nil)
    if not i or i == 0 or not Cam.cams[i] then Log.info("key camera: select or look through a scene camera first (C makes one from the view)"); return end
    Seq.key_camera(i, math.floor(c.t or Seq.t))
end
-- framing overlay: letterbox (0 | 1.85 | 2 | 2.39), rule of thirds, safe areas, centre cross
ops.cam_overlay = function(c) for k, v in pairs(c.fields or {}) do Cam.overlay[k] = v end end
-- camera path aims at a character while it moves (addr or false)
ops.cammove_lookat = function(c)
    local s = Seq.current; local tr = s and s.tracks[c.track]
    if not (tr and tr.kind == "cammove") then return end
    local a = c.addr and Actor.get(c.addr)
    tr.lookat_name = a and a.name or nil
    if c.offset then tr.lookat_offset = c.offset end
    tr.cur = nil
end
ops.remove_cam_key = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and tr.kind == "cammove" then Seq.remove_cam_key(tr, c.t) end end
ops.seq_set = function(c)
    local s = Seq.current or Seq.new("sequence")
    if c.length then s.length = math.max(60, math.floor(c.length)) end
    if c.loop ~= nil then s.loop = c.loop and true or false end
    if c.name then s.name = c.name end
end
ops.seq_release = function() Seq.release() end
-- undo / redo (whole-document snapshots, see history.lua)
ops.undo = function() History.undo() end
ops.redo = function() History.redo() end
-- playback range (in/out) and preview speed
-- time selection: a / b = In / Out, fin / fout = falloff frames around it (Motion Editor)
ops.seq_range = function(c)
    if c.clear then Seq.set_range(nil, nil); return end
    if c.a or c.b then Seq.set_range(c.a, c.b) end
    if c.fin or c.fout then
        if not (Seq.current and Seq.current.range) then Seq.set_range(math.floor(Seq.t), math.floor(Seq.t) + 1) end
        Seq.set_falloff(c.fin, c.fout)
    end
end
ops.seq_speed = function(c) Seq.speed = math.max(0.1, math.min(4, tonumber(c.value) or 1)) end
-- easing of keys: items = {{track, id?, t}}, ease = smooth|linear|hold|in|out|cubic
ops.set_ease = function(c) Seq.set_ease(c.items or { { track = c.track, id = c.id, t = c.t } }, c.ease or "smooth") end
-- dope sheet: several keys at once
ops.move_keys = function(c) Seq.move_keys(c.items, math.floor((c.dt or 0) + 0.5)) end
ops.scale_keys = function(c) Seq.scale_keys(c.items, c.pivot or 0, c.factor or 1) end
ops.remove_keys = function(c) Seq.remove_keys(c.items) end
ops.set_key_value = function(c) Seq.set_key_value(c.items or { { track = c.track, id = c.id, t = c.t } }, c.field, tonumber(c.value) or 0) end
ops.copy_keys = function(c) B.key_clipboard = Seq.copy_keys(c.items); B.data_dirty = true end
ops.paste_keys = function(c) local a = Actor.get(c.addr or B.sel.actor); Seq.paste_keys(B.key_clipboard, math.floor(c.t or Seq.t), a) end
-- audio guide track (played by the Studio, muxed by the renderer)
ops.seq_audio = function(c)
    if c.clear then Seq.set_audio(nil) return end
    local a = {}
    if c.path then a.path = c.path; a.name = c.name or c.path:match("[^/\\]+$") end
    if c.offset ~= nil then a.offset = math.floor(tonumber(c.offset) or 0) end
    if c.volume ~= nil then a.volume = tonumber(c.volume) or 1 end
    if c.duration ~= nil then a.duration = tonumber(c.duration) end
    Seq.set_audio(a)
end
ops.add_track = function(c)
    local a = c.addr and actor_by_addr(c.addr) or Actor.get(B.sel.actor)
    if c.kind == "camera" then Seq.add_camera_track()
    elseif a and c.kind == "anim" then Seq.track_for_actor(a, c.layer or 0, true)
    elseif a and c.kind == "pose" then Seq.pose_track_for_actor(a, true)
    elseif a and c.kind == "xform" then Seq.xform_track_for_actor(a, true) end
end
ops.remove_track = function(c) if c.idx then Seq.remove_track(c.idx) end end
ops.add_clip = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local path = c.path or Catalog.path_for_bank(c.bank); if not path then return end
    local tr = Seq.track_for_actor(a, c.layer or 0, true)
    local name, endframe = c.name, c.endframe
    if not name then
        for _, m in ipairs(a:motions(c.bank)) do if m.id == c.mot then name, endframe = m.name, m.endframe end end
    end
    local clip = Seq.add_clip(tr, { start = math.floor(c.start or Seq.t), path = path, mot = c.mot, name = name, endframe = endframe, blend = c.blend or 10, speed = c.speed or 1, dur = c.dur })
    -- locomotion clips get their travel baked right away (walk / run / jog / dash / sprint / step)
    local n = (name or ""):lower()
    if B.auto_rm ~= false and (tr.layer or 0) == 0 and (n:find("walk") or n:find("run") or n:find("jog") or n:find("dash") or n:find("sprint") or n:find("step")) and not n:find("stairs") then
        B.rm_queue[#B.rm_queue + 1] = { track = tr, clip = clip }
    end
end
B.rm_queue = {}
B.auto_rm = true
ops.auto_root_motion = function(c) B.auto_rm = c.value and true or false end
local function bake_clip(tr, clip, cb)
    local a = Actor.get(tr.actor); if not (a and a:valid()) then return false end
    local bank = tr.bank
    if not bank then
        for id, b in pairs(a.banks) do if b.path == clip.path and b.ready then bank = id end end
    end
    if not bank then return false end
    -- the character stands where the clip starts: that is the base of the travel
    local p = a.xform:call("get_Position"); local r = a.xform:call("get_Rotation")
    local base = { pos = { p.x, p.y, p.z }, rot = { r.w, r.x, r.y, r.z } }
    return RM.bake(a, bank, clip.mot, clip.endframe, tr.layer or 0, function(curve, dist)
        if curve then clip.rm = curve; clip.base = base; clip.rm_dist = dist else clip.rm = nil end
        tr.cur = nil; B.data_dirty = true
        if not Seq.playing then Seq.evaluate(Seq.t, false) end
        if cb then cb(curve, dist) end
    end)
end
ops.bake_root_motion = function(c)
    local cl, tr = find_clip(c.track, c.id); if not cl then return end
    if c.clear then cl.rm = nil; cl.base = nil; cl.rm_dist = nil; tr.cur = nil; return end
    B.rm_queue[#B.rm_queue + 1] = { track = tr, clip = cl }
end
E.on_frame(function()
    if RM.baking or #B.rm_queue == 0 or Seq.playing then return end
    local job = table.remove(B.rm_queue, 1)
    local a = Actor.get(job.track.actor)
    if not (a and a:valid()) then return end
    -- wait for the motlist
    local ready = false
    for _, b in pairs(a.banks) do if b.path == job.clip.path and b.ready then ready = true end end
    if not ready then
        if (job.tries or 0) < 300 then job.tries = (job.tries or 0) + 1; table.insert(B.rm_queue, 1, job) end
        return
    end
    Seq.seek(job.clip.start)
    if not bake_clip(job.track, job.clip, job.on_done) then Log.warn("root motion: could not bake %s", tostring(job.clip.name)) end
end)
ops.update_clip = function(c)
    local cl, tr = find_clip(c.track, c.id); if not cl then return end
    for k, v in pairs(c.fields or {}) do
        if k == "start" then cl.start = math.max(0, math.floor(v + 0.5))
        elseif k == "dur" then cl.dur = math.max(1, v)
        elseif k == "offset" or k == "speed" or k == "blend" or k == "loop" or k == "weight" or k == "fade_in" or k == "fade_out" or k == "mode" then cl[k] = v end
    end
    table.sort(tr.clips, function(x, y) return x.start < y.start end)
    tr.cur = nil
end
ops.remove_clip = function(c) local cl, tr = find_clip(c.track, c.id); if cl then Seq.remove_clip(tr, cl.id) end; if B.sel.clip and B.sel.clip.id == c.id then B.sel.clip = nil end end
ops.select_clip = function(c) B.sel.clip = (c.track and c.id) and { track = c.track, id = c.id, kind = c.kind } or nil end
ops.add_cut = function(c) Seq.add_cut(Seq.add_camera_track(), math.floor(c.t or Seq.t), c.cam or B.sel.camera) end
ops.update_cut = function(c)
    local s = Seq.current; if not s then return end
    local tr = s.tracks[c.track]; if not tr or tr.kind ~= "camera" then return end
    for _, cut in ipairs(tr.cuts) do
        if cut.id == c.id then
            if c.start then cut.start = math.max(0, math.floor(c.start + 0.5)) end
            if c.cam then cut.cam = c.cam end
        end
    end
    table.sort(tr.cuts, function(x, y) return x.start < y.start end); tr.cur = nil
end
ops.remove_cut = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and tr.kind == "camera" then Seq.remove_cut(tr, c.id) end end
ops.remove_xform_key = function(c) local s = Seq.current; local tr = s and s.tracks[c.track]; if tr and tr.kind == "xform" then Seq.remove_xform_key(tr, c.t) end end
ops.remove_pose_key = function(c) local cl = find_clip(c.track, c.id); if cl then Pose.remove_key(cl.keys, c.t) end end
ops.move_pose_key = function(c) local cl = find_clip(c.track, c.id); if cl then Seq.move_pose_key(cl, c.t, c.new_t) end end

-- cameras
ops.cam_capture = function(c) local i = Cam.capture(c.name); if i then B.sel.camera = i end end
ops.cam_orbit = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then B.sel.camera = Cam.add_orbit(c.name or ("Orbit " .. a.name), a.addr) end end
ops.cam_lookat = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then B.sel.camera = Cam.add_lookat(c.name or ("Look at " .. a.name), a.addr) end end
ops.cam_follow = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then B.sel.camera = Cam.add_follow(c.name or ("Follow " .. (a.display_name or a.name)), a.addr) end end
ops.cam_preset = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local i = Cam.add_preset(c.kind or "medium", a.addr, c.name)
    if i then B.sel.camera = i; Seq.cam_follow = false; Cam.set_active(i); Cam.overlay.letterbox = 2.39 end
end
ops.cam_select = function(c) B.sel.camera = c.i or 0 end
ops.cam_live = function(c) Seq.cam_follow = false; Seq.scene_view = false; if c.i and c.i > 0 then Cam.set_active(c.i); B.sel.camera = c.i else Cam.stop() end end
-- viewport: the Work Camera (free, never rendered, never keyed) ...
ops.cam_work = function(c)
    Seq.cam_follow = false; Seq.scene_view = false
    if c.value ~= false and c.from_view then Cam.work_from_view() end
    Cam.set_work(c.value ~= false)
end
-- ... or the Scene Camera: whatever the film shows here (its cut, else the shot's camera), during playback too
ops.cam_scene = function()
    Cam.work_on = false
    Seq.scene_view = true
    Seq.cam_follow = true
    local i = Seq.film_camera(Seq.t)
    if i then Cam.set_active(i); B.sel.camera = i end
end
ops.cam_fly = function(c)
    if c.value then
        Seq.cam_follow = false
        -- nothing to look through: fly the work camera, so no scene camera moves by accident
        if not Cam.active then Seq.scene_view = false; Cam.set_work(true) end
    end
    Cam.set_fly(c.value and true or false, { speed = c.speed, sens = c.sens })
end
ops.cam_stop = function() Cam.stop() end
ops.cam_remove = function(c) if c.i then Cam.remove(c.i); if B.sel.camera == c.i then B.sel.camera = 0 elseif (B.sel.camera or 0) > c.i then B.sel.camera = B.sel.camera - 1 end end end
ops.cam_update = function(c)
    if Cam.cams[c.i or B.sel.camera or 0] then Cam.cams[c.i or B.sel.camera].auto = nil end
    local cam = Cam.cams[c.i or B.sel.camera]; if not cam then return end
    for k, v in pairs(c.fields or {}) do
        if k == "target" then Cam.set_target(cam, v, 0)
        elseif k == "euler" then local q = E.quat_from_euler_deg(v[1], v[2], v[3]); cam.rot = { q.w, q.x, q.y, q.z }
        elseif k == "roll" then cam.roll = tonumber(v) or 0
        elseif k == "follow" then cam.follow = { tonumber(v[1]) or 0, tonumber(v[2]) or 1.5, tonumber(v[3]) or -2.2 }; cam._eye = nil
        elseif k == "smooth" then cam.smooth = math.max(0, math.min(0.95, tonumber(v) or 0.12))
        elseif k == "mode" and (v == "static" or v == "lookat" or v == "follow" or v == "orbit") then
            if v == "static" and cam.mode ~= "static" then local pos, rot = Cam.evaluate(cam, 0); if pos then cam.pos = { pos.x, pos.y, pos.z }; cam.rot = { rot.w, rot.x, rot.y, rot.z } end end
            cam.mode = v; cam._eye = nil
        elseif k == "shake" then cam.shake = (v and (v.amp or v.rot)) and { amp = tonumber(v.amp) or 0, rot = tonumber(v.rot) or 0, freq = tonumber(v.freq) or 1.5 } or nil
        elseif k == "name" or k == "fov" or k == "pos" or k == "radius" or k == "height" or k == "speed" or k == "offset" then cam[k] = v end
    end
end
ops.cam_recapture = function(c) local cam = Cam.cams[c.i or B.sel.camera]; local pose = Cam.game_pose(); if cam and pose then cam.auto = nil; cam.pos = pose.pos; if cam.mode == "static" then cam.rot = pose.rot end end end

-- look-at
ops.lookat_set = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local s = Lookat.setup(a)
    for k, v in pairs(c.fields or {}) do
        if k == "flip" then s.forward_sign = v and -1 or 1
        elseif k == "enabled" or k == "kind" or k == "target" or k == "weight" or k == "max_deg" or k == "smooth" or k == "neck_share" then s[k] = v end
    end
end

-- poser
ops.select_joint = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a)
    w.joint = c.name
    if c.name and not layer.joints[c.name] then local q = Pose.read_local(a, c.name); if q then layer.joints[c.name] = q end end
    local q = c.name and layer.joints[c.name]
    if q then w.euler = { E.euler_deg(q) } end
end
ops.set_joint_euler = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a)
    local name = c.name or w.joint; if not name then return end
    w.euler = { c.euler[1], c.euler[2], c.euler[3] }
    layer.joints[name] = E.quat_from_euler_deg(c.euler[1], c.euler[2], c.euler[3])
    if w.joint ~= name then w.joint = name end
    if B.autokey then Seq.keyframe(a, math.floor(Seq.t), { [name] = layer.joints[name] }) end
end
ops.reset_joint = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a); local name = c.name or w.joint; if not name then return end
    local base = layer.base[name] or Pose.read_local(a, name)
    if base then layer.joints[name] = base; w.euler = { E.euler_deg(base) } end
end
ops.remove_joint = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a); local name = c.name or w.joint; if name then layer.joints[name] = nil; if w.joint == name then w.joint = nil end end
end
ops.mirror_joint = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a); local name = c.name or w.joint; if not name or not layer.joints[name] then return end
    local other = nil
    if name:sub(1, 2) == "L_" then other = "R_" .. name:sub(3) elseif name:sub(1, 2) == "R_" then other = "L_" .. name:sub(3) end
    if other then local q = layer.joints[name]; layer.joints[other] = E.quat(q.w, q.x, -q.y, -q.z) end
end
ops.clear_pose = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then local w, layer = working(a); layer.joints = {}; w.saved_joints = layer.joints; w.joint = nil end end
ops.pose_weight = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then local w, layer = working(a); layer.weight = c.value or 1 end end
ops.keyframe_pose = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then local w, layer = working(a); if next(layer.joints) then if c.t then Seq.keyframe(a, math.floor(c.t), layer.joints) else X.commit_pose_edit(a, layer.joints, layer) end end end end
ops.load_key = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local tr = Seq.pose_track_for_actor(a, false); if not tr then return end
    local w, layer = working(a)
    for _, cl in ipairs(tr.clips) do
        for _, k in ipairs(cl.keys or {}) do
            if math.abs(cl.start + k.t - (c.t or Seq.t)) < 1 then
                layer.joints = {}
                for jn, qv in pairs(k.joints) do layer.joints[jn] = Pose.to_q(qv) end
                w.saved_joints = layer.joints
            end
        end
    end
end
ops.save_pose = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not (a and c.name) then return end
    local w, layer = working(a)
    local joints = layer.joints
    if c.filter and c.filter ~= "working" then joints = X.capture_pose(a, c.filter) end -- subset of the live pose (fingers, upper body, ...)
    Pose.save_static(c.name, joints); B.data_dirty = true
end
ops.load_pose = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not (a and c.name) then return end
    local w, layer = working(a)
    local j = Pose.load_static(c.name); if not j then return end
    if c.merge then for n, q in pairs(j) do layer.joints[n] = q end else layer.joints = j; w.saved_joints = j end
    w.saved_joints = layer.joints
end
-- freeze the current animated pose (or a part of it) into the working layer so it can be keyed / edited
ops.capture_pose = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local w, layer = working(a)
    local j, n = X.capture_pose(a, c.filter or "all")
    for name, q in pairs(j) do layer.joints[name] = q end
    w.saved_joints = layer.joints
    Log.info("captured %d joints (%s) into the working pose of %s", n, tostring(c.filter or "all"), a.name)
end
-- attachments: prop follows a character's joint (R_Wep / L_Wep are the weapon sockets)
ops.attach = function(c)
    local obj = Actor.get(c.addr or B.sel.actor); local a = c.actor and Actor.get(c.actor)
    if not (obj and a) then return end
    if c.keep then X.attach_keep(obj, a, c.joint or "R_Wep") else X.attach(obj, a, c.joint or "R_Wep", c.pos and Vector3f.new(c.pos[1], c.pos[2], c.pos[3]) or nil, c.euler and E.quat_from_euler_deg(c.euler[1], c.euler[2], c.euler[3]) or nil) end
end
ops.detach = function(c) local obj = Actor.get(c.addr or B.sel.actor); if obj then X.detach(obj) end end
ops.attach_offset = function(c)
    local obj = Actor.get(c.addr or B.sel.actor); local at = obj and X.attachment_of(obj); if not at then return end
    if c.pos then at.off_pos = Vector3f.new(c.pos[1], c.pos[2], c.pos[3]) end
    if c.euler then at.off_rot = E.quat_from_euler_deg(c.euler[1], c.euler[2], c.euler[3]) end
end
-- overlays / modes
ops.overlay_skeleton = function(c) X.show_skeleton = c.value and true or false end
-- empty stage: hide + freeze every game character (player included); Director cast stays
ops.stage_clean = function(c) X.set_stage_clean(c.value) end
-- clicking props in the picture: the scan has to be running for them to be candidates
ops.pick_props = function(c)
    B.pick_props = c.value ~= false
    if B.pick_props then B.want_objects = true; refresh_scan(true) end
    B.data_dirty = true
end

-- ---------------------------------------------------------------------------------------------
-- constraints: hands that hold things, feet that stay planted, heads that follow (constraint.lua)
-- ---------------------------------------------------------------------------------------------
ops.constraint_add = function(c)
    local target = nil
    if c.target_actor then target = { actor = c.target_actor, joint = c.target_joint }
    elseif c.point then target = { point = c.point } end
    -- "hold with both hands" is two pins on the same target, one per hand
    local chains = c.both and { "L_Hand", "R_Hand" } or { c.chain or "R_Hand" }
    for _, chain in ipairs(chains) do
        Constraint.add({ kind = c.kind or "ik_pin", actor = c.addr or B.sel.actor, chain = chain, target = target,
                         offset = c.offset, weight = c.weight, range = c.range, name = c.name })
    end
    B.data_dirty = true
end
ops.constraint_update = function(c) Constraint.update(c.id, c.fields); B.data_dirty = true end
ops.constraint_remove = function(c) Constraint.remove(c.id); B.data_dirty = true end
ops.constraint_range = function(c)
    local con = Constraint.get(c.id); if not con then return end
    if c.clear then con.range = nil else
        local s = Seq.current
        local r = s and s.range
        con.range = r and { r[1], r[2] } or nil
    end
    B.data_dirty = true
end

-- ---------------------------------------------------------------------------------------------
-- ragdoll: let a character go limp (a fall, a body on the floor), then put them back
-- ---------------------------------------------------------------------------------------------
ops.ragdoll = function(c)
    local a = Actor.get(c.addr or B.sel.actor)
    if not (a and a:valid()) then return end
    local rd = E.component(a.go, "via.dynamics.Ragdoll")
    if not rd then Log.warn("ragdoll: %s has none", tostring(a.name)); B.data_dirty = true; return end
    local on = c.value ~= false
    a.ragdoll = on or nil
    pcall(function()
        if on then
            a:set_puppet(true)                       -- the AI must not drive a limp body
            rd:call("set_KeyFrameControl", false)    -- stop following the animation
            rd:call("set_MotorControl", c.motor ~= false)
            rd:call("set_MotorStrength", E.f(c.strength or 0.15, 0.15))
            rd:call("set_UpdatePosture", true)
            rd:call("set_RootTransferMotion", true)
        else
            rd:call("set_KeyFrameControl", true)     -- animation drives the bones again
            rd:call("set_KeyFrameControlStrength", 1.0)
            rd:call("set_MotorControl", false)
            rd:call("set_RootTransferMotion", false)
            rd:call("resetTransform", 0)
        end
    end)
    Log.info("ragdoll %s: %s", tostring(a.name), on and "limp" or "back to animation")
    B.data_dirty = true
end
ops.ragdoll_strength = function(c)
    local a = Actor.get(c.addr or B.sel.actor)
    local rd = a and a:valid() and E.component(a.go, "via.dynamics.Ragdoll")
    if rd then pcall(function() rd:call("set_MotorStrength", E.f(c.value or 0.15, 0.15)) end) end
end

ops.actor_visible = function(c) local a = Actor.get(c.addr or B.sel.actor); if a then X.set_visible(a, c.value ~= false) end; B.data_dirty = true end
-- stages: any area of the game without a save file (see stage.lua)
ops.stage_list = function() Stage.list(true); B.data_dirty = true end
ops.stage_go = function(c)
    local entry = c.stage and { stage = c.stage, name = c.name, pos = c.pos } or nil
    if not entry then return end
    if not entry.pos then for _, e in ipairs(Stage.list()) do if e.stage == entry.stage then entry.pos = e.pos; entry.name = entry.name or e.name end end end
    local ok, err = Stage.go(entry)
    if not ok then Log.warn("stage: %s", tostring(err)) end
end
ops.stage_cancel = function() Stage.cancel() end
ops.stage_expand = function(c) Log.try("stage expand", Stage.load_around, c.stage or (Stage.status and Stage.status.stage), c.value ~= false) end
-- game audio preview: post a Wwise event on the system object (plays when its bank is loaded)
B.preview_sound = nil
-- ---------------------------------------------------------------------------------------------
-- Game sound: a five-channel mixer over Wwise's volume parameters (the same ones the options menu
-- drives, read from soundlib.SoundManager._VolGameParIdList). Whatever is audible while a render
-- records is what lands in the video, so this is the mixing desk for the final cut: drop the music,
-- keep the voices, mute the effects.
-- ---------------------------------------------------------------------------------------------
B.mix = { master = 1, music = 1, sfx = 1, voice = 1, cutscene = 1 }
local MIX_ORDER = { "master", "music", "sfx", "voice", "cutscene" }
local mix_ids = nil

local function volume_ids()
    if mix_ids then return mix_ids end
    local ok, ids = pcall(function()
        local t = sdk.find_type_definition("soundlib.SoundManager")
        local arr = sdk.call_native_func(nil, t, "get_VolGameParIdList")
        local out = {}
        for i = 0, (arr and arr:get_size() or 0) - 1 do out[#out + 1] = arr:get_element(i) end
        return out
    end)
    mix_ids = (ok and ids) or {}
    return mix_ids
end

function B.apply_mix()
    local ids = volume_ids()
    if #ids == 0 then return false end
    local t = sdk.find_type_definition("soundlib.SoundManager")
    local go = sdk.call_native_func(nil, t, "get_SystemGameObjId")
    local set = t:get_method("setRtpcValue")
    for i, key in ipairs(MIX_ORDER) do
        local id = ids[i]
        if id then
            local v = math.max(0, math.min(1, B.mix[key] or 1)) * 100
            pcall(function() set:call(nil, go, id, v, false, 0.0, true) end)
        end
    end
    return true
end

ops.sound_mix = function(c)
    if c.reset then for _, key in ipairs(MIX_ORDER) do B.mix[key] = 1 end end
    for _, key in ipairs(MIX_ORDER) do
        local v = c.fields and c.fields[key]
        if v ~= nil then B.mix[key] = math.max(0, math.min(1, v)) end
    end
    local ok = B.apply_mix()
    Log.info("sound mix: master %.2f music %.2f sfx %.2f voice %.2f%s", B.mix.master, B.mix.music, B.mix.sfx, B.mix.voice, ok and "" or " (no volume parameters)")
    B.data_dirty = true
end

-- The container that owns a trigger plays it the way the game does (bank loaded, positioned, mixed).
-- Without one, a bare postEvent on the system object stays silent, so we look for the owner first.
local function container_for(trigger_id)
    local scene = sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene")
    if not scene then return nil end
    local arr = scene:call("findComponents(System.Type)", sdk.typeof("soundlib.SoundContainer"))
    for i = 0, (arr and arr:get_size() or 0) - 1 do
        local c = arr:get_element(i)
        local datas = c and c:call("get_AllTriggerInfoListData")
        for d = 0, (datas and datas:call("get_Count") or 0) - 1 do
            local list = datas:call("get_Item", d):get_field("_TriggerInfoList")
            for k = 0, (list and list:call("get_Count") or 0) - 1 do
                if list:call("get_Item", k):get_field("_TriggerId") == trigger_id then return c end
            end
        end
    end
    return nil
end

ops.sound_preview = function(c)
    local t = sdk.find_type_definition("soundlib.SoundManager")
    if not (t and (c.event or c.trigger)) then B.preview_sound_error = "Sound preview unavailable"; return end
    ops.sound_stop({})
    local how, err = "none", nil
    if c.trigger then
        local cont = Log.try("sound container", container_for, c.trigger)
        if cont then
            local ok, e = pcall(function() cont:call("trigger(System.UInt32)", c.trigger) end)
            if ok then how = "container"; B.preview_sound = { container = cont, trigger = c.trigger } else err = tostring(e) end
        end
    end
    if how == "none" and c.event then
        local ok, e = pcall(function()
            local goid = sdk.call_native_func(nil, t, "get_SystemGameObjId")
            t:get_method("postEvent"):call(nil, goid, c.event, 0, 0, false, 0, 0)
            B.preview_sound = { go = goid, event = c.event }
        end)
        if ok then how = "post" else err = tostring(e) end
    end
    B.preview_sound_error = err
    B.sound_state = { event = c.event, trigger = c.trigger, how = how, at = os.clock() }
    B.data_dirty = true
end

ops.sound_stop = function()
    local t = sdk.find_type_definition("soundlib.SoundManager")
    local p = B.preview_sound
    if t and p then
        if p.container and p.trigger then
            pcall(function() p.container:call("stopTriggered(System.UInt32, via.GameObject, System.UInt32)", p.trigger, nil, 0) end)
        elseif p.go and p.event then
            pcall(function() t:get_method("stopEvent(System.UInt64, System.UInt32)"):call(nil, p.go, p.event) end)
        end
    end
    B.preview_sound = nil
    B.sound_state = nil
    B.data_dirty = true
end
ops.overlay_onion = function(c) X.onion = c.value and true or false end
ops.motion_edit = function(c) X.motion_edit = c.value and true or false end
-- shots
ops.shot_add = function(c)
    local s = Seq.current or Seq.new("sequence")
    local a = c.a or (s.range and s.range[1]) or 0
    local b = c.b or (s.range and s.range[2]) or s.length
    X.shot_add(c.name, a, b, c.cam or (Cam.active or nil))
end
ops.shot_remove = function(c) X.shot_remove(c.id) end
ops.shot_update = function(c) X.shot_update(c.id, c.fields or {}) end
ops.shot_go = function(c)
    for _, sh in ipairs(X.shots()) do
        if sh.id == c.id then
            Seq.set_range(sh.a, sh.b); Seq.pause(); drop_unkeyed_edits(); Seq.seek(sh.a)
            if sh.cam and Cam.cams[sh.cam] then Cam.set_active(sh.cam); B.sel.camera = sh.cam end
        end
    end
end
-- export the selected character's animation for Blender (rig + pose keys + travel keys)
ops.export_anim = function(c)
    local a = Actor.get(c.addr or B.sel.actor); if not a then return end
    local rig = Import.capture_rig(a)
    local out = { rig = rig, fps = Seq.current and Seq.current.fps or 60, pose = {}, travel = {}, name = a.display_name or a.name }
    local ptr = Seq.pose_track_for_actor(a, false)
    if ptr then for _, cl in ipairs(ptr.clips) do for _, k in ipairs(cl.keys or {}) do out.pose[#out.pose + 1] = { t = cl.start + k.t, joints = k.joints, ease = k.ease } end end end
    local xtr = Seq.xform_track_for_actor(a, false)
    if xtr then for _, k in ipairs(xtr.keys) do out.travel[#out.travel + 1] = { t = k.t, pos = k.pos, rot = k.rot } end end
    table.sort(out.pose, function(x, y) return x.t < y.t end)
    local file = "director/export/" .. (a.display_name or a.name):gsub("[^%w]", "_") .. "_" .. os.date("%H%M%S") .. ".json"
    local ok, err = pcall(json.dump_file, file, out, 0)
    if ok then B.last_export = file; Log.info("exported %d pose keys, %d travel keys -> %s", #out.pose, #out.travel, file) else Log.err("export failed: %s", tostring(err)) end
    B.data_dirty = true
end

-- gizmo
ops.gizmo = function(c)
    if c.enabled ~= nil then Gizmo.enabled = c.enabled and true or false end
    if c.target then Gizmo.target = c.target end
    local tool = c.tool -- ("op" is the command envelope itself: never read it here)
    if tool and (tool == "move" or tool == "rotate" or tool == "scale") then Gizmo.op = tool; Gizmo.bone_op = (tool == "move") and "move" or "rotate" end
    if c.mode then Gizmo.mode = c.mode end
    if c.snap then Gizmo.snap = c.snap end
    if c.show_bones ~= nil then Gizmo.show_bones = c.show_bones and true or false end
end

-- project / tests
ops.project_save = function(c) Project.save(c.name or Project.current); B.data_dirty = true end
ops.project_load = function(c) if c.name then Project.load(c.name); B.data_dirty = true end end
-- fresh project: everything you built goes (spawned cast + props, cameras, lights, sequence); the game's own characters are released
ops.project_new = function(c)
    Seq.release()
    for _, a in ipairs(Actor.all()) do
        if a.spawned then
            if a.cast then Cast.destroy(a) else pcall(function() a.go:call("destroy", a.go) end); a:forget() end
        else a:forget() end
    end
    Cast.destroy_all()
    Cam.stop()
    for i = #Cam.cams, 1, -1 do Cam.remove(i) end
    Lights.clear()
    Seq.new("sequence")
    B.sel.actor, B.sel.camera, B.sel.light, B.sel.clip, B.sel.multi = nil, 0, nil, nil, {}
    Project.current = (c.name and c.name ~= "") and c.name or "untitled"
    B.data_dirty = true
end
ops.run_test = function(c)
    local fn = ({ pause = Selftest.test_pause, layering = Selftest.test_layering, cutscene = Selftest.test_cutscene, camera = Selftest.test_camera,
                  sequence = Selftest.test_sequence, pose = Selftest.test_pose, placement = Selftest.test_placement })[c.name]
    if fn then fn() end
end

local function dump(v, depth)
    depth = depth or 0
    local tv = type(v)
    if tv == "nil" or tv == "number" or tv == "boolean" or tv == "string" then return v end
    if tv == "table" then
        if depth >= 4 then return "{...}" end
        local n = 0
        for _ in pairs(v) do n = n + 1 end
        if n == #v then
            local out = {}
            for i = 1, math.min(#v, 200) do out[i] = dump(v[i], depth + 1) end
            if #v > 200 then out[#out + 1] = ("... %d more"):format(#v - 200) end
            return out
        end
        local out, cnt = {}, 0
        for k, x in pairs(v) do
            cnt = cnt + 1
            if cnt > 200 then out["..."] = "more"; break end
            out[tostring(k)] = dump(x, depth + 1)
        end
        return out
    end
    if tv == "userdata" then
        local ok, s = pcall(function()
            if v.get_type_definition then
                local td = v:get_type_definition()
                return ("<%s @%s>"):format(td and td:get_full_name() or "?", tostring(v.get_address and v:get_address() or ""))
            end
            return tostring(v)
        end)
        return ok and s or tostring(v)
    end
    return tostring(v)
end

-- dev REPL (commands only ever come from the local Studio host)
ops.eval = function(c)
    local env = setmetatable({ E = E, Actor = Actor, Cam = Cam, Seq = Seq, B = B, Log = Log, Catalog = Catalog, Pose = Pose, Lookat = Lookat,
        Gizmo = Gizmo, Project = Project, Selftest = Selftest, dump = dump }, { __index = _G })
    local fn, err = load(c.code or "", "=eval", "t", env)
    local out
    if not fn then
        out = { ok = false, error = err }
    else
        local ok, res = pcall(fn)
        if ok then out = { ok = true, result = dump(res) } else out = { ok = false, error = tostring(res) } end
    end
    out.id = c.cid
    B.eval = out
    B.data_dirty = true
end

-- commands that do not change the document (no undo step)
local READONLY = {}
for _, op in ipairs({ "ping", "refresh", "select_actor", "add_actor", "select_clip", "cam_select", "select_joint", "catalog_search", "load_motlist", "gizmo", "eval",
    "run_test", "mesh_search", "mesh_preview", "mesh_preview_clear", "cast_preview", "cast_preview_clear", "want_objects", "hud", "show_cameras", "test_focus", "edit_mode", "cam_fly", "cam_live", "cam_stop", "game_freeze", "autokey",
    "seq_play", "seq_pause", "seq_toggle", "seq_stop", "seq_seek", "seq_speed", "render_clock_begin", "render_clock_step", "render_clock_end", "project_save", "undo", "redo", "copy_keys", "release_actor", "release_all",
    "set_puppet", "set_paused", "drive", "record", "overlay_skeleton", "overlay_onion", "motion_edit", "shot_go", "export_anim", "export_rig", "light_select", "select_clear", "group_release", "group_remove", "duplicate_cast", "path_finish", "stage_clean", "spawn_cast", "spawn_mesh", "destroy_object", "destroy_cast_all", "cast_preset", "cast_physics", "cast_place", "play", "set_speed", "set_frame", "stage_list", "stage_go", "stage_cancel", "stage_expand", "thumb_stage", "sound_preview", "sound_stop", "sound_mix", "pick_props", "ragdoll_strength", "project_new", "actor_visible" }) do READONLY[op] = true end

local function run_commands()
    local ok, data = pcall(json.load_file, B.dir .. "cmd.json")
    if not ok or not data or not data.cmds then return end
    for _, c in ipairs(data.cmds) do
        local id = tonumber(c.cid or c.id) or 0 -- cid = envelope id (0.4.2+); payload may carry its own id (clip/cut)
        if id > B.last_cmd_id then
            B.last_cmd_id = id
            local fn = ops[c.op]
            if fn and not READONLY[c.op] then History.mark(c.op) end
            if fn then
                local okc, err = pcall(fn, c)
                if not okc then Log.err("cmd %s failed: %s", tostring(c.op), tostring(err)) end
            else
                Log.warn("unknown command: %s", tostring(c.op))
            end
            B.data_dirty = true
        end
    end
end

-------------------------------------------------------------------------------
-- per frame
-------------------------------------------------------------------------------
local frame = 0
local last_joint_sig, last_bank_sig = "", ""
E.on_frame(function()
    frame = frame + 1
    Log.try("bridge cmds", run_commands)
    Log.try("stage", Stage.tick)
    Log.try("constraints", Constraint.solve, Seq.t)
    if (E.render_clock and E.render_clock.active) or frame % B.state_every == 0 then
        Log.try("bridge state", function() json.dump_file(B.dir .. "state.json", build_state(), -1) end)
    end
    refresh_scan(false)
    -- detect data changes that don't go through commands (banks finishing, actor selection)
    local a = cur_actor()
    local sig = ""
    if a then
        for bank_id, b in pairs(a.banks) do if b.ready then sig = sig .. bank_id .. "," end end
        sig = sig .. "|" .. tostring(a.addr)
    end
    if sig ~= last_bank_sig then last_bank_sig = sig; B.data_dirty = true end
    if B.data_dirty and frame % 6 == 0 then
        B.data_dirty = false
        B.data_ver = B.data_ver + 1
        Log.try("bridge data", function() json.dump_file(B.dir .. "data.json", build_data(), -1) end)
    end
end)

-- Turntable rig for the hovered prop. The mesh's own local bounding box is unreliable (often a unit cube, and
-- a frame stale after a swap), so every frame we measure the *world* box it actually occupies and nudge scale
-- and position towards the target: whatever the prop's real size, it ends up palm-sized in the lower right,
-- turning slowly. A wrong reading corrects itself on the next frame instead of filling the screen.
local function quat_inv(q) return E.quat(q.w, -q.x, -q.y, -q.z) end
local rig_go, rig_frame = nil, 0
E.on_frame(function()
    rig_frame = rig_frame + 1
    if not (rig_go and E.valid(rig_go)) or rig_frame % 15 == 0 then
        local ok, scene = pcall(function() return sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene") end)
        rig_go = ok and scene and scene:call("findGameObject(System.String)", "Director_Preview") or nil
    end
    if not (rig_go and E.valid(rig_go)) then return end
    pcall(function()
        local xf = rig_go:call("get_Transform")
        local comp = rig_go:call("getComponent(System.Type)", sdk.typeof("via.render.Mesh"))
        if not (comp and comp:call("get_MeshReady")) then return end
        local thumb = B.thumb.on
        xf:call("set_LocalRotation", thumb and E.quat_from_euler_deg(B.thumb.pitch, B.thumb.yaw, 0)
                                          or E.quat_from_euler_deg(-14, (os.clock() * 42) % 360, 0))
        comp:call("updateBoundingBox")
        local box = comp:call("get_WorldAABB")
        local lo, hi = box:get_field("minpos"), box:get_field("maxpos")
        local span = math.max(hi.x - lo.x, hi.y - lo.y, hi.z - lo.z)
        if span <= 1e-5 or span > 1e5 then return end
        local ls = xf:call("get_LocalScale")
        local fit = thumb and B.thumb.fit or PREVIEW_FIT
        local anchor = thumb and B.thumb.anchor or PREVIEW_ANCHOR
        local want = math.max(0.0005, math.min(500, ls.x * fit / span))
        local converged = math.abs(want / ls.x - 1) <= 0.02
        B.thumb.stable = converged and (B.thumb.stable + 1) or 0
        -- publish as soon as the mesh is measurable: the capture loop waits on this name, and a prop that
        -- happens to need no scale correction would otherwise never announce itself
        local info = B.mesh_preview_info
        if not (info and info.name == B.preview_name) then
            B.mesh_preview_info = { name = B.preview_name, size = { (hi.x - lo.x) / ls.x, (hi.y - lo.y) / ls.x, (hi.z - lo.z) / ls.x } }
            B.data_dirty = true
        end
        if not converged then
            local sc = ls.x + (want - ls.x) * 0.6
            xf:call("set_LocalScale", Vector3f.new(sc, sc, sc))
            B.mesh_preview_info = { name = B.preview_name, size = { (hi.x - lo.x) / ls.x, (hi.y - lo.y) / ls.x, (hi.z - lo.z) / ls.x } }
            B.data_dirty = true
        end
        if converged and B.thumb.stable == 3 then B.data_dirty = true end
        local par = xf:call("get_Parent"); if not par then return end
        local ppos, prot = par:call("get_Position"), par:call("get_Rotation")
        local mid = Vector3f.new((lo.x + hi.x) * 0.5 - ppos.x, (lo.y + hi.y) * 0.5 - ppos.y, (lo.z + hi.z) * 0.5 - ppos.z)
        local delta = anchor - E.rotate(quat_inv(prot), mid)
        local lp = xf:call("get_LocalPosition")
        xf:call("set_LocalPosition", Vector3f.new(lp.x + delta.x * 0.6, lp.y + delta.y * 0.6, lp.z + delta.z * 0.6))
    end)
end)

E.on_frame(function()
    if not Gizmo.enabled then return end
    local s = Seq.current; if not s then return end
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "path" then pcall(PathMod.draw, tr, tr.actor == B.sel.actor) end
    end
end)

Log.info("bridge ready (reframework/data/%s)", B.dir)
return B
