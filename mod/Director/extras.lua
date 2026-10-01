-- Director :: extras — attachments (props on bones), skeleton / onion-skin overlay, ground snapping, pose capture,
-- shots, motion-edit (time-selection) key commit. Small features that share the actor / sequence model.
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local Seq = require("Director.sequence")
local Pose = require("Director.pose")

local X = { attachments = {}, show_skeleton = false, onion = false, motion_edit = false }

local function qrot(q, v)
    local m = q:to_mat4()
    return Vector3f.new(m[0].x * v.x + m[1].x * v.y + m[2].x * v.z, m[0].y * v.x + m[1].y * v.y + m[2].y * v.z, m[0].z * v.x + m[1].z * v.y + m[2].z * v.z)
end

-------------------------------------------------------------------------------
-- attachments: an object (spawned prop / any actor of kind object) follows a joint of a character
-------------------------------------------------------------------------------
function X.attach(obj, actor, joint_name, off_pos, off_rot)
    X.detach(obj)
    local j = actor:joint(joint_name)
    if not j then return false, "no joint " .. tostring(joint_name) end
    X.attachments[#X.attachments + 1] = { obj = obj, actor = actor, joint = joint_name,
        off_pos = off_pos or Vector3f.new(0, 0, 0), off_rot = off_rot or E.quat_identity() }
    if obj.root_lock then obj.root_lock = nil end
    Log.info("attached %s to %s/%s", obj.name, actor.name, joint_name)
    return true
end

function X.detach(obj)
    for i = #X.attachments, 1, -1 do if X.attachments[i].obj == obj then table.remove(X.attachments, i) end end
end

-- offsets so the object keeps its current world pose relative to the joint
function X.attach_keep(obj, actor, joint_name)
    local j = actor:joint(joint_name); if not j then return false end
    local jp = j:call("get_Position"); local jr = j:call("get_Rotation")
    local op = obj.xform:call("get_Position"); local orr = obj.xform:call("get_Rotation")
    local inv = E.quat(jr.w, -jr.x, -jr.y, -jr.z)
    local d = Vector3f.new(op.x - jp.x, op.y - jp.y, op.z - jp.z)
    return X.attach(obj, actor, joint_name, qrot(inv, d), (inv * orr):normalized())
end

function X.attachment_of(obj)
    for _, at in ipairs(X.attachments) do if at.obj == obj then return at end end
end

local function update_attachments()
    for i = #X.attachments, 1, -1 do
        local at = X.attachments[i]
        if not (at.obj:valid() and at.actor:valid()) then table.remove(X.attachments, i)
        else
            local j = at.actor:joint(at.joint)
            if j then
                pcall(function()
                    local jp = j:call("get_Position"); local jr = j:call("get_Rotation")
                    local q = E.quat(jr.w, jr.x, jr.y, jr.z)
                    local wp = qrot(q, at.off_pos)
                    at.obj.xform:call("set_Position", Vector3f.new(jp.x + wp.x, jp.y + wp.y, jp.z + wp.z))
                    at.obj.xform:call("set_Rotation", (q * at.off_rot):normalized())
                end)
            end
        end
    end
end
E.after_motion(update_attachments)
E.on_frame(update_attachments)

function X.serialize_attachments()
    local out = {}
    for _, at in ipairs(X.attachments) do
        out[#out + 1] = { obj = at.obj.name, actor = at.actor.name, joint = at.joint, off_pos = { at.off_pos.x, at.off_pos.y, at.off_pos.z }, off_rot = { at.off_rot.w, at.off_rot.x, at.off_rot.y, at.off_rot.z } }
    end
    return out
end

function X.deserialize_attachments(list, resolve)
    for _, d in ipairs(list or {}) do
        local obj, a = resolve(d.obj), resolve(d.actor)
        if obj and a then X.attach(obj, a, d.joint, Vector3f.new(d.off_pos[1], d.off_pos[2], d.off_pos[3]), E.quat(d.off_rot[1], d.off_rot[2], d.off_rot[3], d.off_rot[4])) end
    end
end

-------------------------------------------------------------------------------
-- ground: physics ray cast straight down (method names probed once; nil when the engine says no)
-------------------------------------------------------------------------------
local ray_warned = false
local function ray_points(a, b)
    local hits = {}
    local ok, err = pcall(function()
        local sys = sdk.get_native_singleton("via.physics.System")
        local st = sdk.find_type_definition("via.physics.System")
        if not (sys and st) then return end
        -- The walkable world answers to the player's own collision filter (layer 2, mask 796 — read off
        -- via.physics.CharacterController); 8 and 20 add props / walls for the wall clamp.
        for _, f in ipairs({ { 2, 796 }, { 8 }, { 20 } }) do
            local q = sdk.create_instance("via.physics.CastRayQuery"):add_ref()
            local r = sdk.create_instance("via.physics.CastRayResult"):add_ref()
            q:call("setRay", a, b)
            q:call("enableAllHits")
            local fi = q:call("get_FilterInfo")
            fi:call("set_Layer", f[1]); if f[2] then fi:call("set_MaskBits", f[2]) end
            sdk.call_native_func(sys, st, "castRay(via.physics.CastRayQuery, via.physics.CastRayResult)", q, r)
            for i = 0, math.min(r:call("get_NumContactPoints") - 1, 31) do
                local cp = r:call("getContactPoint", i)
                local p = cp and cp:get_field("Position")
                if p then hits[#hits + 1] = E.v3(p) end
            end
        end
    end)
    if not ok and not ray_warned then ray_warned = true; Log.warn("ground ray failed: %s", tostring(err)) end
    return hits
end

function X.ground_y(x, y, z, up, down)
    local floor
    for _, p in ipairs(ray_points(Vector3f.new(x, y + (up or 1.5), z), Vector3f.new(x, y - (down or 4), z))) do
        if p.y <= y + 0.35 and p.y >= y - (down or 4) and (not floor or p.y > floor) then floor = p.y end
    end
    if floor then return floor end
    -- The game floor in some areas is navigation geometry, not a physics hit.
    -- Nearby spawns can safely share the player's current grounded height.
    local body = E.player_body()
    if body then
        local p = body:call("get_Transform"):call("get_Position")
        local dx, dz = x - p.x, z - p.z
        if dx * dx + dz * dz < 6.25 and math.abs(y - p.y) < 1.5 then return p.y end
    end
    return nil
end

-- Clamp horizontal character travel before the first wall. Two rays keep feet and torso
-- out of scenery while the vertical ground ray handles slopes and steps.
function X.resolve_motion(from, to, padding)
    if not from or not to then return to end
    local dx, dz = to.x - from.x, to.z - from.z
    local len = math.sqrt(dx * dx + dz * dz)
    if len < 0.001 then return to end
    local nx, nz = dx / len, dz / len
    local best, bestd
    for _, h in ipairs({ 0.35, 1.15 }) do
        for _, p in ipairs(ray_points(Vector3f.new(from.x, from.y + h, from.z), Vector3f.new(to.x, to.y + h, to.z))) do
            local d = math.sqrt((p.x - from.x) ^ 2 + (p.z - from.z) ^ 2)
            if d > 0.6 and d <= len + 0.05 and (not bestd or d < bestd) then best, bestd = p, d end
        end
    end
    if not best then return to end
    local d = math.max(0, bestd - (padding or 0.28))
    return Vector3f.new(from.x + nx * d, to.y, from.z + nz * d)
end

-------------------------------------------------------------------------------
-- pose capture: the character's current (animated) pose into the working layer, optionally a subset
-------------------------------------------------------------------------------
X.FILTERS = {
    all = function() return true end,
    fingers = function(n) return n:match("^[LR]_%a+F%d$") or n:match("^[LR]_Thumb%d$") or n:match("^[LR]_Palm$") end,
    left_hand = function(n) return n:match("^L_%a+F%d$") or n:match("^L_Thumb%d$") or n:match("^L_Palm$") end,
    right_hand = function(n) return n:match("^R_%a+F%d$") or n:match("^R_Thumb%d$") or n:match("^R_Palm$") end,
    upper = function(n) return n:match("^Spine") or n:match("^Neck") or n == "Head" or n:match("^[LR]_Shoulder") or n:match("^[LR]_UpperArm") or n:match("^[LR]_Forearm") or n:match("^[LR]_Hand") end,
    lower = function(n) return n:match("^[LR]_Thigh") or n:match("^[LR]_Shin") or n:match("^[LR]_Foot") or n:match("^[LR]_Toe") or n == "Hip" end,
    face = function(n) return n:match("Eye") or n:match("Jaw") or n:match("Lip") or n:match("Brow") or n:match("Cheek") or n:match("Tongue") or n:match("Nose") end,
}

function X.capture_pose(a, filter)
    local f = X.FILTERS[filter or "all"] or X.FILTERS.all
    local out, n = {}, 0
    for _, name in ipairs(a:joint_names()) do
        if f(name) and name ~= "root" and name ~= "Null_Offset" then
            local q = Pose.read_local(a, name)
            if q then out[name] = E.quat(q.w, q.x, q.y, q.z); n = n + 1 end
        end
    end
    return out, n
end

-------------------------------------------------------------------------------
-- motion edit: a pose edit becomes a bump over the playback range (keys at range start / playhead / range end)
-------------------------------------------------------------------------------
function X.commit_pose_edit(a, joints, layer)
    local s = Seq.current
    local t = math.floor(Seq.t)
    if not (X.motion_edit and s and s.range) then Seq.keyframe(a, t, joints); return "key" end
    local a0, b0 = s.range[1], s.range[2]
    local fin, fout = 0, 0
    if s.falloff then fin, fout = s.falloff[1] or 0, s.falloff[2] or 0 end
    -- ends: what the joints did underneath the edit (animation + lower layers), so the edit fades back into it
    local ends = {}
    for name in pairs(joints) do
        local base = layer and layer.base and layer.base[name]
        if base then ends[name] = E.quat(base.w, base.x, base.y, base.z) end
    end
    local tr = Seq.pose_track_for_actor(a, true)
    local function existing(at)
        for _, c in ipairs(tr.clips) do if at >= c.start and at <= c.start + c.dur then return true end end
        return false
    end
    if fin > 0 or fout > 0 then
        -- SFM time selection with falloff: the edit holds from In to Out and blends back into the motion over
        -- the falloff on either side
        if t < a0 - fin or t > b0 + fout then Seq.keyframe(a, t, joints); return "key" end
        local lo, hi = math.max(0, a0 - math.max(1, fin)), b0 + math.max(1, fout)
        if next(ends) then
            if not existing(lo) then Seq.keyframe(a, lo, ends) end
            if not existing(hi) then Seq.keyframe(a, hi, ends) end
        end
        Seq.keyframe(a, a0, joints)
        Seq.keyframe(a, b0, joints)
        return "hold"
    end
    if t <= a0 or t >= b0 then Seq.keyframe(a, t, joints); return "key" end
    if next(ends) then
        if not existing(a0) then Seq.keyframe(a, a0, ends) end
        if not existing(b0) then Seq.keyframe(a, b0, ends) end
    end
    Seq.keyframe(a, t, joints)
    return "bump"
end

-------------------------------------------------------------------------------
-- empty stage ("director mode"): the player, enemies, companions and every other game character are hidden and
-- frozen; only Director cast members stay. Re-applied every second because the game streams characters in.
-------------------------------------------------------------------------------
X.stage_clean = false
X.stage_hidden = {}   -- [addr] = { go=, update= (was updating) }

local function draw_tree(go, on, depth)
    if depth > 8 or not E.valid(go) then return end
    pcall(function() go:call("set_DrawSelf", on) end)
    local xf = go:call("get_Transform")
    local n = xf and xf:call("get_ChildCount") or 0
    for i = 0, n - 1 do
        local ch = xf:call("getChild", i); local cgo = ch and ch:call("get_GameObject")
        if cgo then draw_tree(cgo, on, depth + 1) end
    end
end

local function stage_apply()
    local body = E.player_body()
    for _, c in ipairs(Actor.scan(nil)) do
        local go = c.go
        local addr = go:get_address()
        local name = c.name or ""
        -- only characters: character bodies (ch*_body: player, enemies, NPCs, cows) and animal gimmicks (gm03_*: chickens,
        -- crows, rats, bats). Doors, traps, ladders and other gimmicks also carry a Motion + FSM and must stay.
        local is_char = name:match("^ch%w+_body$") ~= nil or name:match("^gm03_") ~= nil
        if is_char and not X.stage_hidden[addr] then
            local rec = { go = go, player = (go == body) }
            pcall(function() rec.update = go:call("get_UpdateSelf") end)
            draw_tree(go, false, 0)
            if not rec.player then pcall(function() go:call("set_UpdateSelf", false) end) end -- the player keeps updating (camera / streaming), it is frozen instead
            X.stage_hidden[addr] = rec
        end
    end
    if body then E.set_player_frozen(true) end
end

local function stage_restore()
    for addr, rec in pairs(X.stage_hidden) do
        if E.valid(rec.go) then
            draw_tree(rec.go, true, 0)
            if not rec.player then pcall(function() rec.go:call("set_UpdateSelf", rec.update ~= false) end) end
        end
    end
    X.stage_hidden = {}
end

-- per-actor visibility (outliner eye): draw tree off / on, remembered in a.hidden
function X.set_visible(a, on)
    if not (a and a:valid()) then return end
    draw_tree(a.go, on and true or false, 0)
    a.hidden = (not on) or nil
end

function X.set_stage_clean(on)
    on = on and true or false
    if on == X.stage_clean then return end
    X.stage_clean = on
    if on then stage_apply() else stage_restore(); E.set_player_frozen(false) end
    Log.info("empty stage %s", on and "on" or "off")
end

local stage_tick = 0
E.on_frame(function()
    if not X.stage_clean then return end
    stage_tick = stage_tick + 1
    if stage_tick % 60 == 0 then pcall(stage_apply) end
end)

-------------------------------------------------------------------------------
-- shots: named ranges with a camera (render / play one shot at a time)
-------------------------------------------------------------------------------
function X.shots() local s = Seq.current; if not s then return {} end; s.shots = s.shots or {}; return s.shots end
function X.shot_add(name, a, b, cam)
    local list = X.shots()
    local sh = { id = Seq.next_id, name = name or ("Shot " .. (#list + 1)), a = math.floor(a), b = math.floor(b), cam = cam }
    Seq.next_id = Seq.next_id + 1
    list[#list + 1] = sh
    table.sort(list, function(x, y) return x.a < y.a end)
    return sh
end
function X.shot_remove(id) local list = X.shots(); for i, sh in ipairs(list) do if sh.id == id then table.remove(list, i) end end end
function X.shot_update(id, f) for _, sh in ipairs(X.shots()) do if sh.id == id then for k, v in pairs(f) do sh[k] = v end end end end

-------------------------------------------------------------------------------
-- overlay: skeleton lines of the selected character; onion skin = skeleton at the previous / next pose key (FK)
-------------------------------------------------------------------------------
local function skeleton_lines(a, override, color)
    -- override: name -> Quaternion local rotation (for the ghost); FK from local positions + rotations
    local names = a:joint_names()
    local world = {}
    local function jw(name)
        if world[name] then return world[name] end
        local j = a:joint(name); if not j then return nil end
        local parent
        pcall(function() local p = j:call("get_Parent"); if p then parent = p:call("get_Name") end end)
        local lp = j:call("get_LocalPosition")
        local lq = override and override[name] or j:call("get_LocalRotation")
        local pw = parent and jw(parent)
        if pw then
            local off = qrot(pw.rot, Vector3f.new(lp.x, lp.y, lp.z))
            world[name] = { pos = pw.pos + off, rot = (pw.rot * lq):normalized(), parent = parent }
        else
            local wp = j:call("get_Position"); local wr = j:call("get_Rotation")
            world[name] = { pos = Vector3f.new(wp.x, wp.y, wp.z), rot = E.quat(wr.w, wr.x, wr.y, wr.z), parent = parent }
        end
        return world[name]
    end
    for _, n in ipairs(names) do
        if not n:match("Twist") and not n:match("Wep") and not n:match("Offset") then
            local w = jw(n)
            if w and w.parent and world[w.parent] then
                local s1, s2 = draw.world_to_screen(world[w.parent].pos), draw.world_to_screen(w.pos)
                if s1 and s2 then draw.line(s1.x, s1.y, s2.x, s2.y, color) end
            end
        end
    end
end

X.selected = nil -- function() -> actor (bridge)
function X.draw()
    if not (X.show_skeleton or X.onion) then return end
    local Gizmo = package.loaded["Director.gizmo"]
    if not (Gizmo and Gizmo.enabled) then return end
    local a = X.selected and X.selected()
    if not (a and a:valid()) then return end
    if X.show_skeleton then pcall(skeleton_lines, a, nil, 0xCCFFFFFF) end
    if X.onion then
        local tr = Seq.pose_track_for_actor(a, false)
        if tr then
            local t = Seq.t
            local prev, nxt
            for _, c in ipairs(tr.clips) do
                for _, k in ipairs(c.keys or {}) do
                    local at = c.start + k.t
                    if at < t - 0.5 and (not prev or at > prev.at) then prev = { at = at, k = k } end
                    if at > t + 0.5 and (not nxt or at < nxt.at) then nxt = { at = at, k = k } end
                end
            end
            local function ghost(entry, col)
                local o = {}
                for n, v in pairs(entry.k.joints) do o[n] = Pose.to_q(v) end
                pcall(skeleton_lines, a, o, col)
            end
            if prev then ghost(prev, 0x66FF8A65) end
            if nxt then ghost(nxt, 0x666EA8FF) end
        end
    end
end
E.on_frame(function() pcall(X.draw) end)

return X
