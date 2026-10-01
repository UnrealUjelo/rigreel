-- Director :: sequence (timeline) model, transport and per-frame evaluation
local Log = require("Director.log")
local E = require("Director.engine")
local Catalog = require("Director.catalog")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Pose = require("Director.pose")
local RM = require("Director.rootmotion")
local PathMod = require("Director.path")

local Seq = {
    current = nil,
    playing = false,
    t = 0.0,           -- playhead in frames (float)
    last_clock = nil,
    next_id = 1,
    speed = 1.0,       -- playback rate (0.25 .. 2), preview only
}
Seq.ease = Pose.ease

-------------------------------------------------------------------------------
-- model
-------------------------------------------------------------------------------
function Seq.new(name)
    local s = { name = name or "sequence", fps = 60, length = 600, loop = false, tracks = {}, range = nil, audio = nil }
    Seq.current = s
    Seq.t = 0; Seq.playing = false
    return s
end

local function ensure()
    if not Seq.current then Seq.new("sequence") end
    return Seq.current
end

function Seq.add_anim_track(actor, layer)
    local s = ensure()
    local tr = { kind = "anim", name = actor.name, actor_name = actor.name, actor = actor.addr, layer = layer or 0, clips = {}, cur = nil, hold = nil, bank = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

function Seq.add_camera_track()
    local s = ensure()
    for _, tr in ipairs(s.tracks) do if tr.kind == "camera" then return tr end end
    local tr = { kind = "camera", name = "Camera", cuts = {}, cur = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

-- camera move track: keys = { {t=, pos={x,y,z}, rot={w,x,y,z}, fov=} } for one camera index
function Seq.cammove_track(cam_idx, create)
    local s = ensure()
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "cammove" and tr.cam == cam_idx then return tr end
    end
    if not create then return nil end
    local c = Cam.cams[cam_idx]
    local tr = { kind = "cammove", name = (c and c.name or ("Camera " .. cam_idx)) .. " moves", cam = cam_idx, keys = {}, cur = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

-- key the camera's current pose (incl. fly edits) at absolute time t
function Seq.key_camera(cam_idx, t)
    if cam_idx == 0 then Log.info("the work camera is never keyed - press C to make a scene camera from the view"); return nil end
    local c = Cam.cams[cam_idx]
    if not c then return nil end
    local pos, rot, fov = Cam.evaluate(c, 0)
    if not pos then return nil end
    local tr = Seq.cammove_track(cam_idx, true)
    local k
    for _, x in ipairs(tr.keys) do if math.abs(x.t - t) < 0.5 then k = x end end
    if not k then k = { t = t }; tr.keys[#tr.keys + 1] = k end
    k.pos = { pos.x, pos.y, pos.z }; k.rot = { rot.w, rot.x, rot.y, rot.z }; k.fov = fov or c.fov
    k.roll = c.roll or 0
    k.dof_f = c.dof_f; k.dof_focus = c.dof_focus
    table.sort(tr.keys, function(x, y) return x.t < y.t end)
    local s = ensure()
    if t + 1 > s.length then s.length = math.ceil(t + 1) end
    c.auto = nil
    return tr, k
end

function Seq.remove_cam_key(tr, t)
    for i, k in ipairs(tr.keys) do if math.abs(k.t - t) < 0.5 then table.remove(tr.keys, i); return true end end
    return false
end

-- a camera was removed from Cam.cams: drop its tracks/cuts, shift higher indices
function Seq.on_camera_removed(idx)
    local s = Seq.current
    if not s then return end
    for i = #s.tracks, 1, -1 do
        local tr = s.tracks[i]
        if tr.kind == "cammove" then
            if tr.cam == idx then table.remove(s.tracks, i) elseif tr.cam > idx then tr.cam = tr.cam - 1 end
        elseif tr.kind == "camera" then
            for j = #tr.cuts, 1, -1 do
                local cut = tr.cuts[j]
                if cut.cam == idx then table.remove(tr.cuts, j) elseif cut.cam > idx then cut.cam = cut.cam - 1 end
            end
            tr.cur = nil
        end
    end
end

function Seq.track_for_actor(actor, layer, create)
    local s = ensure()
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "anim" and tr.actor == actor.addr and tr.layer == (layer or 0) then return tr end
    end
    if create then return Seq.add_anim_track(actor, layer) end
    return nil
end

function Seq.add_pose_track(actor)
    local s = ensure()
    local tr = { kind = "pose", name = actor.name .. " pose", actor_name = actor.name, actor = actor.addr, clips = {}, cur = nil, layer = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

function Seq.pose_track_for_actor(actor, create)
    local s = ensure()
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "pose" and tr.actor == actor.addr then return tr end
    end
    if create then return Seq.add_pose_track(actor) end
    return nil
end

-- pose clip: { id, start, dur, keys = {...}, weight, fade_in, fade_out, loop, mode }
function Seq.add_pose_clip(tr, c)
    c.id = Seq.next_id; Seq.next_id = Seq.next_id + 1
    c.keys = c.keys or {}
    c.weight = c.weight or 1.0; c.fade_in = c.fade_in or 10; c.fade_out = c.fade_out or 10
    c.loop = c.loop or false; c.mode = c.mode or "override"
    c.dur = math.max(1, c.dur or 60)
    tr.clips[#tr.clips + 1] = c
    table.sort(tr.clips, function(x, y) return x.start < y.start end)
    tr.cur = nil
    local s = ensure()
    if c.start + c.dur > s.length then s.length = math.ceil(c.start + c.dur) end
    return c
end

-- record a key (joints: name -> Quaternion) at absolute time t for an actor; creates track/clip as needed.
function Seq.keyframe(actor, t, joints)
    local tr = Seq.pose_track_for_actor(actor, true)
    local clip = nil
    for _, c in ipairs(tr.clips) do
        if t >= c.start and t <= c.start + c.dur then clip = c end
    end
    if not clip then
        -- extend the nearest clip that ends before t, else create a new one
        local prev = nil
        for _, c in ipairs(tr.clips) do if c.start <= t then prev = c end end
        if prev and t - (prev.start + prev.dur) < 120 then
            clip = prev; clip.dur = t - clip.start + 1
        else
            clip = Seq.add_pose_clip(tr, { start = t, dur = 1 })
        end
    end
    local key = Pose.set_key(clip.keys, t - clip.start, joints)
    if t - clip.start + 1 > clip.dur then clip.dur = t - clip.start + 1 end
    local s = ensure()
    if clip.start + clip.dur > s.length then s.length = math.ceil(clip.start + clip.dur) end
    tr.cur = nil
    return tr, clip, key
end

-- transform (placement) track: keys = { {t=, pos={x,y,z}, rot={w,x,y,z}} } sorted by t
function Seq.xform_track_for_actor(actor, create)
    local s = ensure()
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "xform" and tr.actor == actor.addr then return tr end
    end
    if not create then return nil end
    local tr = { kind = "xform", name = actor.name .. " move", actor_name = actor.name, actor = actor.addr, keys = {}, cur = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

function Seq.key_transform(actor, t, pos, rot)
    local tr = Seq.xform_track_for_actor(actor, true)
    pos = pos or E.v3(actor.xform:call("get_Position"))
    rot = rot or actor.xform:call("get_Rotation")
    for _, k in ipairs(tr.keys) do
        if math.abs(k.t - t) < 0.5 then k.pos = { pos.x, pos.y, pos.z }; k.rot = { rot.w, rot.x, rot.y, rot.z }; return tr, k end
    end
    local k = { t = t, pos = { pos.x, pos.y, pos.z }, rot = { rot.w, rot.x, rot.y, rot.z } }
    tr.keys[#tr.keys + 1] = k
    table.sort(tr.keys, function(x, y) return x.t < y.t end)
    local s = ensure()
    if t + 1 > s.length then s.length = math.ceil(t + 1) end
    return tr, k
end

-- retime a key on a keyed track (xform / cammove)
function Seq.move_key(tr, t, new_t)
    for _, k in ipairs(tr.keys or {}) do
        if math.abs(k.t - t) < 0.5 then
            k.t = math.max(0, math.floor(new_t + 0.5))
            table.sort(tr.keys, function(x, y) return x.t < y.t end)
            local s = ensure()
            if k.t + 1 > s.length then s.length = math.ceil(k.t + 1) end
            tr.cur = nil
            return true
        end
    end
    return false
end

-- retime a pose key inside its clip (grows the clip if needed)
function Seq.move_pose_key(clip, t, new_t)
    for _, k in ipairs(clip.keys or {}) do
        if math.abs(k.t - t) < 0.5 then
            k.t = math.max(0, math.floor(new_t + 0.5))
            table.sort(clip.keys, function(x, y) return x.t < y.t end)
            if k.t + 1 > clip.dur then clip.dur = k.t + 1 end
            return true
        end
    end
    return false
end

function Seq.remove_xform_key(tr, t)
    for i, k in ipairs(tr.keys) do if math.abs(k.t - t) < 0.5 then table.remove(tr.keys, i); return true end end
    return false
end

local function detach_pose_layer(tr)
    if tr.kind == "pose" and tr.layer then
        local a = tr.actor and Actor.get(tr.actor)
        if a then Pose.remove_layer(a, tr.layer) end
        tr.layer = nil
    end
end

function Seq.remove_track(idx)
    local s = ensure()
    local tr = s.tracks[idx]
    if tr and tr.kind == "anim" then
        local a = Actor.get(tr.actor)
        if a then a:release() end
    end
    if tr then detach_pose_layer(tr) end
    table.remove(s.tracks, idx)
end

-- clip: { id, start, dur, path, mot, name, endframe, offset, speed, blend, loop }
function Seq.add_clip(tr, c)
    c.id = Seq.next_id; Seq.next_id = Seq.next_id + 1
    c.offset = c.offset or 0; c.speed = c.speed or 1.0; c.blend = c.blend or 10; c.loop = c.loop or false
    c.dur = math.max(1, c.dur or ((c.endframe and c.endframe > 0) and c.endframe / math.abs(c.speed) or 120))
    tr.clips[#tr.clips + 1] = c
    table.sort(tr.clips, function(x, y) return x.start < y.start end)
    tr.cur = nil -- force re-evaluation
    local s = ensure()
    if c.start + c.dur > s.length then s.length = math.ceil(c.start + c.dur) end
    return c
end

function Seq.remove_clip(tr, id)
    for i, c in ipairs(tr.clips) do if c.id == id then table.remove(tr.clips, i); tr.cur = nil; return end end
end

function Seq.add_cut(tr, start, cam_idx)
    for _, cut in ipairs(tr.cuts) do
        if math.abs(cut.start - start) < 0.5 then cut.cam = cam_idx; tr.cur = nil; return cut end
    end
    local cut = { id = Seq.next_id, start = start, cam = cam_idx }
    Seq.next_id = Seq.next_id + 1
    tr.cuts[#tr.cuts + 1] = cut
    table.sort(tr.cuts, function(x, y) return x.start < y.start end)
    tr.cur = nil
    return cut
end

function Seq.remove_cut(tr, id)
    for i, c in ipairs(tr.cuts) do if c.id == id then table.remove(tr.cuts, i); tr.cur = nil; return end end
end

-------------------------------------------------------------------------------
-- evaluation
-------------------------------------------------------------------------------
local function clip_at(tr, t)
    local found = nil
    for _, c in ipairs(tr.clips) do
        if t >= c.start and t < c.start + c.dur then found = c end
    end
    return found
end

local function clip_frame(c, t)
    local f = c.offset + (t - c.start) * c.speed
    local e = c.endframe or 0
    if e > 0 then
        if c.loop then f = f % e else f = math.max(0, math.min(f, e - 0.01)) end
    end
    return math.max(0, f)
end
Seq.clip_frame = clip_frame

local function cut_at(tr, t)
    local found = nil
    for _, c in ipairs(tr.cuts) do if c.start <= t then found = c end end
    return found
end

local function track_actor(tr)
    local a = tr.actor and Actor.get(tr.actor)
    if a and a:valid() and not a.detached then return a end
    return nil
end

-- actors released by the user stay out of the sequence until the transport is used again
local function reattach_tracked()
    local s = Seq.current
    if not s then return end
    for _, tr in ipairs(s.tracks) do
        local a = tr.actor and Actor.get(tr.actor)
        if a then a.detached = nil end
    end
end

local function eval_anim(tr, t, playing)
    local a = track_actor(tr)
    if not a then return end
    local clip = clip_at(tr, t)
    local entry = clip and clip.path and require("Director.catalog").entry(clip.path)
    local facial = clip and a.cast and clip.path and ((entry and entry.g and entry.g:match("^facial")) or clip.path:lower():match("_10%.motlist$")) and true or false
    if clip ~= tr.cur then
        if clip then
            local start_frame = clip_frame(clip, t)
            local function go(bank)
                tr.bank = bank
                a:play(bank, clip.mot, { layer = tr.layer, blend = playing and clip.blend or 0, start = start_frame, speed = clip.speed, puppet = (tr.layer == 0) })
                if not playing and facial then
                    local Cast = require("Director.cast")
                    Cast.pause_facial(a, false)
                    E.defer(function() if not Seq.playing then Cast.pause_facial(a, true) end end, 2)
                elseif not playing then
                    -- let the engine take the new motion for one frame, then hold again
                    a:set_paused(false)
                    E.defer(function() if not Seq.playing then a:set_paused(true) end end, 2)
                end
            end
            a:load_motlist(clip.path, go)
            tr.hold = clip
        end
        tr.cur = clip
    end
    if clip then
        local li
        if facial then li = require("Director.cast").facial_layer(a, tr.bank, tr.layer)
        else li = a:layer_info(tr.layer) end
        if li and li.bank == tr.bank then
            local expected = clip_frame(clip, t)
            if not playing then
                if facial then require("Director.cast").set_facial_frame(a, tr.bank, tr.layer, expected) else a:set_frame(tr.layer, expected) end
            elseif math.abs((li.frame or 0) - expected) > 4 then
                if facial then require("Director.cast").set_facial_frame(a, tr.bank, tr.layer, expected) else a:set_frame(tr.layer, expected) end
            end
        end
    elseif tr.hold and tr.layer == 0 then
        -- gap after a clip: hold that clip's last frame
        local li = a:layer_info(tr.layer)
        if li and li.bank == tr.bank then
            a:set_frame(tr.layer, clip_frame(tr.hold, tr.hold.start + tr.hold.dur))
        end
    end
    -- baked root motion: the character travels along the clip's measured path from where it stood at the clip start
    local rmclip = clip or (tr.hold and t >= tr.hold.start and tr.hold or nil)
    if rmclip and rmclip.rm and rmclip.base and tr.layer == 0 and not rmclip.path_owned then
        local tt = clip and t or (rmclip.start + rmclip.dur)
        local f = rmclip.offset + (tt - rmclip.start) * rmclip.speed
        local x1, z1 = RM.at(rmclip.rm, f, rmclip.loop)
        local x0, z0 = RM.at(rmclip.rm, rmclip.offset, rmclip.loop)
        local bq = E.quat(rmclip.base.rot[1], rmclip.base.rot[2], rmclip.base.rot[3], rmclip.base.rot[4])
        local w = RM.qrot(bq, Vector3f.new(x1 - x0, 0, z1 - z0))
        if not a.root_lock then a:set_root_lock(true) end
        local want = Vector3f.new(rmclip.base.pos[1] + w.x, rmclip.base.pos[2], rmclip.base.pos[3] + w.z)
        local cur = E.v3(a.xform:call("get_Position"))
        if Seq.resolve_motion then want = Seq.resolve_motion(cur, want) or want end
        if Seq.ground_y then local gy = Seq.ground_y(want.x, want.y, want.z); if gy and math.abs(gy - want.y) < 1.5 then want.y = gy end end
        a:set_root_pose(want, bq)
        a.rm_serial = Seq.eval_serial
    end
end

local function fade_weight(lt, dur, fin, fout)
    local w = 1.0
    if fin and fin > 0 and lt < fin then w = math.min(w, lt / fin) end
    if fout and fout > 0 and dur - lt < fout then w = math.min(w, math.max(0, (dur - lt) / fout)) end
    return math.max(0, math.min(1, w))
end

local function eval_pose(tr, t)
    local a = track_actor(tr)
    if not a then return end
    local clip = clip_at(tr, t)
    if not tr.layer or not (a.pose_layers and (function() for _, l in ipairs(a.pose_layers) do if l == tr.layer then return true end end end)()) then
        tr.layer = Pose.add_layer(a, "track:" .. tr.name, clip and clip.mode or "override")
    end
    local layer = tr.layer
    if not clip then layer.enabled = false; tr.cur = nil; return end
    local lt = t - clip.start
    layer.mode = clip.mode or "override"
    layer.joints = Pose.eval_keys(clip.keys, lt, clip.loop)
    layer.weight = (clip.weight or 1) * fade_weight(lt, clip.dur, clip.fade_in, clip.fade_out)
    layer.enabled = true
    tr.cur = clip
end

local function eval_xform(tr, t)
    local a = track_actor(tr)
    if not a or #tr.keys == 0 then return end
    local keys = tr.keys
    local k0, k1 = keys[1], nil
    for i = 1, #keys do
        if keys[i].t <= t then k0 = keys[i]; k1 = keys[i + 1] else break end
    end
    if t < keys[1].t then k0, k1 = keys[1], nil end
    local pos, rot
    local q0 = E.quat(k0.rot[1], k0.rot[2], k0.rot[3], k0.rot[4])
    if k1 and k1.t > k0.t then
        local s = Pose.ease(k0.ease, (t - k0.t) / (k1.t - k0.t))
        pos = Vector3f.new(k0.pos[1] + (k1.pos[1] - k0.pos[1]) * s, k0.pos[2] + (k1.pos[2] - k0.pos[2]) * s, k0.pos[3] + (k1.pos[3] - k0.pos[3]) * s)
        local q1 = E.quat(k1.rot[1], k1.rot[2], k1.rot[3], k1.rot[4])
        rot = Pose.slerp(q0, q1, s)
    else
        pos = Vector3f.new(k0.pos[1], k0.pos[2], k0.pos[3])
        rot = q0
    end
    if not a.root_lock then a:set_root_lock(true) end
    if a.rm_serial == Seq.eval_serial then a:set_root_pose(nil, rot) else a:set_root_pose(pos, rot) end -- a baked clip owns the position
end

local function eval_cammove(tr, t)
    if not (Seq.playing or Seq.cam_follow) then return end
    local c = Cam.cams[tr.cam]
    if not c or #tr.keys == 0 then return end
    local keys = tr.keys
    local k0, k1 = keys[1], nil
    for i = 1, #keys do
        if keys[i].t <= t then k0 = keys[i]; k1 = keys[i + 1] else break end
    end
    if t < keys[1].t then k0, k1 = keys[1], nil end
    local pos, rot, fov
    local q0 = E.quat(k0.rot[1], k0.rot[2], k0.rot[3], k0.rot[4])
    if k1 and k1.t > k0.t then
        local s = Pose.ease(k0.ease, (t - k0.t) / (k1.t - k0.t))
        pos = { k0.pos[1] + (k1.pos[1] - k0.pos[1]) * s, k0.pos[2] + (k1.pos[2] - k0.pos[2]) * s, k0.pos[3] + (k1.pos[3] - k0.pos[3]) * s }
        local q = Pose.slerp(q0, E.quat(k1.rot[1], k1.rot[2], k1.rot[3], k1.rot[4]), s)
        rot = { q.w, q.x, q.y, q.z }
        fov = (k0.fov or c.fov) + ((k1.fov or c.fov) - (k0.fov or c.fov)) * s
        c.roll = (k0.roll or 0) + ((k1.roll or 0) - (k0.roll or 0)) * s
        if k0.dof_f and k1.dof_f then c.dof_f = k0.dof_f + (k1.dof_f - k0.dof_f) * s elseif k0.dof_f ~= nil then c.dof_f = k0.dof_f end
        if type(k0.dof_focus) == "number" and type(k1.dof_focus) == "number" then c.dof_focus = k0.dof_focus + (k1.dof_focus - k0.dof_focus) * s
        elseif k0.dof_focus ~= nil then c.dof_focus = k0.dof_focus end
    else
        pos, rot, fov = { k0.pos[1], k0.pos[2], k0.pos[3] }, { k0.rot[1], k0.rot[2], k0.rot[3], k0.rot[4] }, k0.fov or c.fov
        if k0.roll then c.roll = k0.roll end
        if k0.dof_f ~= nil then c.dof_f = k0.dof_f end
        if k0.dof_focus ~= nil then c.dof_focus = k0.dof_focus end
    end
    if Cam.fly.on and Cam.active == tr.cam then return end -- the pilot has the stick while flying
    if c.mode == "orbit" then c.mode = "static" end
    c.pos = pos
    -- path look-at: the camera moves along its keys but keeps a character framed
    if tr.lookat_name then
        local a = Actor.get_by_name(tr.lookat_name)
        if a and a:valid() then
            local ap = a.xform:call("get_Position")
            local off = tr.lookat_offset or { 0, 1.4, 0 }
            local q = E.look_rotation(Vector3f.new(pos[1], pos[2], pos[3]), Vector3f.new(ap.x + off[1], ap.y + off[2], ap.z + off[3]))
            rot = { q.w, q.x, q.y, q.z }
        end
    end
    if c.mode == "static" then c.rot = rot end
    c.fov = fov
end

-- camera cuts drive the view only while the sequence is playing or right after a seek (Seq.cam_follow),
-- so an idle timeline never steals the camera from the player.
-- The viewport's Work Camera (Cam.work_on) is never moved by the film. The Scene Camera (Seq.scene_view) always
-- follows it, like SFM's viewport locked to the shot camera.
Seq.cam_follow = false
Seq.scene_view = false
local function eval_camera(tr, t)
    if Cam.work_on then tr.cur = nil; return end
    if not (Seq.playing or Seq.cam_follow or Seq.scene_view) then tr.cur = nil; return end
    local cut = cut_at(tr, t)
    if cut ~= tr.cur or (cut and Cam.cams[cut.cam] and (Cam.active ~= cut.cam or not Cam.enabled)) then
        tr.cur = cut
        if cut then Cam.set_active(cut.cam) end
    end
end

-- a film without camera cuts: every shot looks through its own camera
Seq.cur_shot = nil
local function eval_shot_camera(s, t)
    if Cam.work_on or not (Seq.playing or Seq.cam_follow or Seq.scene_view) then Seq.cur_shot = nil; return end
    local cur
    for _, sh in ipairs(s.shots or {}) do if t >= sh.a and t < sh.b then cur = sh end end
    local cam = cur and cur.cam and Cam.cams[cur.cam] and cur.cam or nil
    if cur ~= Seq.cur_shot or (cam and (Cam.active ~= cam or not Cam.enabled)) then
        Seq.cur_shot = cur
        if cam then Cam.set_active(cam) end
    end
end

-- the camera the film shows at t (cut, else the shot's camera), for the UI and the Scene Camera switch
function Seq.film_camera(t)
    local s = Seq.current
    if not s then return nil end
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "camera" then local cut = cut_at(tr, t); if cut and Cam.cams[cut.cam] then return cut.cam end end
    end
    for _, sh in ipairs(s.shots or {}) do
        if t >= sh.a and t < sh.b and sh.cam and Cam.cams[sh.cam] then return sh.cam end
    end
    return nil
end

Seq.eval_serial = 0
function Seq.evaluate(t, playing)
    local s = Seq.current
    if not s then return end
    Seq.eval_serial = Seq.eval_serial + 1
    local cuts = false
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "anim" then Log.try("eval anim", eval_anim, tr, t, playing)
        elseif tr.kind == "pose" then Log.try("eval pose", eval_pose, tr, t)
        elseif tr.kind == "xform" then Log.try("eval xform", eval_xform, tr, t)
        elseif tr.kind == "camera" then cuts = cuts or #tr.cuts > 0; Log.try("eval camera", eval_camera, tr, t)
        elseif tr.kind == "cammove" then Log.try("eval cammove", eval_cammove, tr, t) end
    end
    if not cuts then Log.try("eval shot camera", eval_shot_camera, s, t) end
    -- paths own the character's position: evaluated after everything else
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "path" then Log.try("eval path", PathMod.evaluate, tr, t, Seq.ground_y, Seq.resolve_motion) end
    end
end
Seq.ground_y = nil -- function(x, y, z) -> floor y (extras.ground_y, installed by the bridge)
Seq.resolve_motion = nil -- function(from, to) -> collision-clamped position

function Seq.path_track_for_actor(actor, create)
    local s = ensure()
    for _, tr in ipairs(s.tracks) do if tr.kind == "path" and tr.actor == actor.addr then return tr end end
    if not create then return nil end
    local tr = { kind = "path", name = (actor.display_name or actor.name) .. " path", actor_name = actor.name, actor = actor.addr, points = {}, start = math.floor(Seq.t), dur = 60, speed = 1.35, ease = 0.08, cur = nil }
    s.tracks[#s.tracks + 1] = tr
    return tr
end

-- after points / speed changed: rebuild the curve, derive the duration (unless pinned), keep the walk clip in step
function Seq.path_refresh(tr)
    PathMod.rebuild(tr)
    if not tr.dur_pinned then tr.dur = PathMod.duration_for(tr) end
    local s = ensure()
    if tr.start + tr.dur > s.length then s.length = math.ceil(tr.start + tr.dur) end
    PathMod.sync_clip(tr, Seq)
    tr.cur = nil
end

-------------------------------------------------------------------------------
-- clip editing (anim + pose clips)
-------------------------------------------------------------------------------
local function copy_clip(c)
    local n = {}
    for k, v in pairs(c) do n[k] = v end
    n.id = nil
    if c.keys then
        n.keys = {}
        for i, k in ipairs(c.keys) do
            local j = {}
            for name, v in pairs(k.joints or {}) do j[name] = v end
            n.keys[i] = { t = k.t, joints = j }
        end
    end
    return n
end

local function add_copy(tr, c)
    if tr.kind == "anim" then return Seq.add_clip(tr, c) end
    return Seq.add_pose_clip(tr, c)
end

-- split a clip at absolute time t into two clips
function Seq.split_clip(tr, clip, t)
    local lt = math.floor(t - clip.start + 0.5)
    if lt <= 0 or lt >= clip.dur then return nil end
    local right = copy_clip(clip)
    right.start = clip.start + lt
    right.dur = clip.dur - lt
    if tr.kind == "anim" then
        right.offset = clip_frame(clip, clip.start + lt)
        right.blend = 0
    else
        right.keys = {}
        for _, k in ipairs(clip.keys) do
            if k.t >= lt then right.keys[#right.keys + 1] = { t = k.t - lt, joints = k.joints } end
        end
        for i = #clip.keys, 1, -1 do if clip.keys[i].t >= lt then table.remove(clip.keys, i) end end
        right.fade_in = 0
        clip.fade_out = 0
    end
    clip.dur = lt
    tr.cur = nil
    return add_copy(tr, right)
end

-- cut away the part before (side="left") or after (side="right") absolute time t
function Seq.trim_clip(tr, clip, t, side)
    local lt = math.floor(t - clip.start + 0.5)
    if lt <= 0 or lt >= clip.dur then return false end
    if side == "left" then
        if tr.kind == "anim" then clip.offset = clip_frame(clip, clip.start + lt) end
        if clip.keys then
            for i = #clip.keys, 1, -1 do
                if clip.keys[i].t < lt then table.remove(clip.keys, i) else clip.keys[i].t = clip.keys[i].t - lt end
            end
        end
        clip.start = clip.start + lt
        clip.dur = clip.dur - lt
    else
        if clip.keys then for i = #clip.keys, 1, -1 do if clip.keys[i].t > lt then table.remove(clip.keys, i) end end end
        clip.dur = lt
    end
    table.sort(tr.clips, function(x, y) return x.start < y.start end)
    tr.cur = nil
    return true
end

-- copy of a clip at absolute time t (same track)
function Seq.paste_clip(tr, clip, t)
    local n = copy_clip(clip)
    n.start = math.max(0, math.floor(t))
    return add_copy(tr, n)
end

-- duplicate right after the original
function Seq.duplicate_clip(tr, clip)
    local n = copy_clip(clip)
    n.start = clip.start + clip.dur
    return add_copy(tr, n)
end

-------------------------------------------------------------------------------
-- keys as a selection (dope sheet): items = { {track=idx, id=clipId|nil, t=absolute frame}, ... }
-- xform / cammove keys live on the track; pose keys live in a clip (id) with clip-relative times
-------------------------------------------------------------------------------
local function key_at(list, t)
    for _, k in ipairs(list or {}) do if math.abs(k.t - t) < 0.5 then return k end end
end

local function resolve_items(items)
    local s = Seq.current; if not s then return {} end
    local out, seen = {}, {}
    for _, it in ipairs(items or {}) do
        local tr = s.tracks[it.track]
        if tr then
            local k, clip
            if (tr.kind == "xform" or tr.kind == "cammove") then k = key_at(tr.keys, it.t)
            elseif tr.kind == "pose" then
                for _, c in ipairs(tr.clips) do if c.id == it.id then clip = c end end
                if clip then k = key_at(clip.keys, it.t - clip.start) end
            end
            if k and not seen[k] then seen[k] = true; out[#out + 1] = { key = k, tr = tr, clip = clip } end
        end
    end
    return out
end

local function retime(entry, new_t)  -- new_t absolute
    local k, tr, clip = entry.key, entry.tr, entry.clip
    if clip then
        k.t = math.max(0, math.floor(new_t - clip.start + 0.5))
        if k.t + 1 > clip.dur then clip.dur = k.t + 1 end
    else
        k.t = math.max(0, math.floor(new_t + 0.5))
    end
    tr.cur = nil
end

local function finish_keys(entries)
    local s = ensure()
    local seen = {}
    for _, e in ipairs(entries) do
        local list = e.clip and e.clip.keys or e.tr.keys
        if not seen[list] then seen[list] = true; table.sort(list, function(x, y) return x.t < y.t end) end
        local last = e.clip and (e.clip.start + e.clip.dur) or (e.key.t + 1)
        if last > s.length then s.length = math.ceil(last) end
    end
end

local function abs_t(e) return e.clip and (e.clip.start + e.key.t) or e.key.t end

function Seq.move_keys(items, dt)
    local es = resolve_items(items)
    local times = {}
    for i, e in ipairs(es) do times[i] = abs_t(e) end
    for i, e in ipairs(es) do retime(e, times[i] + dt) end
    finish_keys(es)
    return #es
end

-- scale key times about `pivot` (absolute frame)
function Seq.scale_keys(items, pivot, factor)
    local es = resolve_items(items)
    local times = {}
    for i, e in ipairs(es) do times[i] = abs_t(e) end
    for i, e in ipairs(es) do retime(e, pivot + (times[i] - pivot) * factor) end
    finish_keys(es)
    return #es
end

function Seq.remove_keys(items)
    local es = resolve_items(items)
    for _, e in ipairs(es) do
        local list = e.clip and e.clip.keys or e.tr.keys
        for i = #list, 1, -1 do if list[i] == e.key then table.remove(list, i) end end
        e.tr.cur = nil
    end
    return #es
end

function Seq.set_ease(items, ease)
    local es = resolve_items(items)
    for _, e in ipairs(es) do e.key.ease = (ease ~= "smooth") and ease or nil; e.tr.cur = nil end
    return #es
end

-- graph editor: change one channel of a key. fields: x y z (position), fov / roll (camera), yaw (character facing, degrees)
function Seq.set_key_value(items, field, value)
    local es = resolve_items(items)
    for _, e in ipairs(es) do
        local k = e.key
        if e.tr.kind == "pose" then
        elseif field == "x" or field == "y" or field == "z" then
            k.pos[field == "x" and 1 or field == "y" and 2 or 3] = value
        elseif field == "fov" and e.tr.kind == "cammove" then k.fov = value
        elseif field == "roll" and e.tr.kind == "cammove" then k.roll = value
        elseif field == "yaw" and e.tr.kind == "xform" then
            local q = E.quat_from_euler_deg(0, value, 0); k.rot = { q.w, q.x, q.y, q.z }
        end
        e.tr.cur = nil
    end
    return #es
end

-- clipboard: keys with their content, times relative to the earliest one
function Seq.copy_keys(items)
    local es = resolve_items(items)
    if #es == 0 then return nil end
    local t0 = math.huge
    for _, e in ipairs(es) do t0 = math.min(t0, abs_t(e)) end
    local out = { xform = {}, cammove = {}, pose = {} }
    for _, e in ipairs(es) do
        local k, rel = e.key, abs_t(e) - t0
        if e.tr.kind == "xform" then out.xform[#out.xform + 1] = { t = rel, pos = { k.pos[1], k.pos[2], k.pos[3] }, rot = { k.rot[1], k.rot[2], k.rot[3], k.rot[4] }, ease = k.ease }
        elseif e.tr.kind == "cammove" then out.cammove[#out.cammove + 1] = { t = rel, pos = { k.pos[1], k.pos[2], k.pos[3] }, rot = { k.rot[1], k.rot[2], k.rot[3], k.rot[4] }, fov = k.fov, ease = k.ease, cam = e.tr.cam }
        elseif e.tr.kind == "pose" then
            local j = {}
            for n, v in pairs(k.joints) do j[n] = { v[1], v[2], v[3], v[4] } end
            out.pose[#out.pose + 1] = { t = rel, joints = j, ease = k.ease }
        end
    end
    out.count = #es
    return out
end

-- paste onto `actor` (pose / placement keys) and the pasted cameras' own tracks at absolute time t
function Seq.paste_keys(clip, t, actor)
    if not clip then return 0 end
    local n = 0
    if actor then
        for _, k in ipairs(clip.xform) do
            local _, key = Seq.key_transform(actor, math.floor(t + k.t + 0.5), Vector3f.new(k.pos[1], k.pos[2], k.pos[3]), E.quat(k.rot[1], k.rot[2], k.rot[3], k.rot[4]))
            key.ease = k.ease; n = n + 1
        end
        for _, k in ipairs(clip.pose) do
            local _, _, key = Seq.keyframe(actor, math.floor(t + k.t + 0.5), k.joints)
            if key then key.ease = k.ease end
            n = n + 1
        end
    end
    for _, k in ipairs(clip.cammove) do
        if Cam.cams[k.cam] then
            local tr = Seq.cammove_track(k.cam, true)
            local at = math.floor(t + k.t + 0.5)
            local key = key_at(tr.keys, at)
            if not key then key = { t = at }; tr.keys[#tr.keys + 1] = key end
            key.pos = { k.pos[1], k.pos[2], k.pos[3] }; key.rot = { k.rot[1], k.rot[2], k.rot[3], k.rot[4] }; key.fov = k.fov; key.ease = k.ease
            table.sort(tr.keys, function(x, y) return x.t < y.t end)
            tr.cur = nil; n = n + 1
        end
    end
    return n
end

-------------------------------------------------------------------------------
-- audio: one guide track, played by the Studio (host) in sync with the transport, muxed into renders
-------------------------------------------------------------------------------
function Seq.set_audio(a)
    local s = ensure()
    if not a then s.audio = nil; return end
    s.audio = s.audio or { offset = 0, volume = 1 }
    for k, v in pairs(a) do s.audio[k] = v end
end

-------------------------------------------------------------------------------
-- snapshot / restore (undo)
-------------------------------------------------------------------------------
local function deep(v)
    if type(v) ~= "table" then return v end
    local out = {}
    for k, x in pairs(v) do out[k] = deep(x) end
    return out
end

function Seq.snapshot()
    local s = Seq.current
    if not s then return nil end
    local out = { name = s.name, fps = s.fps, length = s.length, loop = s.loop, range = deep(s.range), falloff = deep(s.falloff), audio = deep(s.audio), shots = deep(s.shots), tracks = {} }
    for i, tr in ipairs(s.tracks) do
        local c = {}
        for k, v in pairs(tr) do
            if k ~= "cur" and k ~= "hold" and k ~= "bank" and k ~= "samples" and not (k == "layer" and tr.kind == "pose") then c[k] = deep(v) end
        end
        out.tracks[i] = c
    end
    return out
end

function Seq.restore(snap)
    if not snap then return end
    local s = Seq.current
    if s then for _, tr in ipairs(s.tracks) do detach_pose_layer(tr) end end
    Seq.current = snap
    for _, tr in ipairs(snap.tracks) do tr.cur = nil; tr.hold = nil; tr.bank = nil end
    if Seq.t > snap.length then Seq.t = snap.length end
    Seq.evaluate(Seq.t, Seq.playing)
end

-------------------------------------------------------------------------------
-- transport
-------------------------------------------------------------------------------
local function set_actors_paused(paused)
    local s = Seq.current; if not s then return end
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "anim" then
            local a = track_actor(tr)
            if a and tr.layer == 0 then a:set_paused(paused) end
        end
    end
end

-- playback range {a, b} (frames): play / loop only inside it. nil = whole sequence
function Seq.set_range(a, b)
    local s = ensure()
    if not a and not b then s.range = nil; s.falloff = nil; return end
    local r = s.range or { 0, s.length }
    if a then r[1] = math.max(0, math.floor(a)) end
    if b then r[2] = math.max(0, math.floor(b)) end
    if r[2] <= r[1] then r[2] = r[1] + 1 end
    s.range = r
end

-- the time selection's falloff (SFM): frames before In / after Out over which an edit blends back into the motion
function Seq.set_falloff(fin, fout)
    local s = ensure()
    local f = s.falloff or { 0, 0 }
    if fin then f[1] = math.max(0, math.floor(fin + 0.5)) end
    if fout then f[2] = math.max(0, math.floor(fout + 0.5)) end
    s.falloff = (f[1] > 0 or f[2] > 0) and f or nil
end

local function play_bounds(s)
    if s.range then return s.range[1], math.min(s.range[2], s.length) end
    return 0, s.length
end

function Seq.play()
    if not Seq.current then return end
    reattach_tracked()
    local a, b = play_bounds(Seq.current)
    if Seq.t >= b or Seq.t < a then Seq.t = a end
    Seq.cam_follow = true
    Seq.playing = true
    Seq.last_clock = os.clock()
    set_actors_paused(false)
    for _, tr in ipairs(Seq.current.tracks) do tr.cur = nil end -- re-issue motions with correct start frames
end

function Seq.pause()
    Seq.playing = false
    set_actors_paused(true)
end

function Seq.stop()
    Seq.playing = false
    Seq.t = 0
    if Seq.current then for _, tr in ipairs(Seq.current.tracks) do tr.cur = nil end end
    set_actors_paused(true)
    Seq.evaluate(Seq.t, false)
end

function Seq.seek(t)
    local s = Seq.current; if not s then return end
    reattach_tracked()
    Seq.cam_follow = true
    Seq.t = math.max(0, math.min(t, s.length))
    if not Seq.playing then Seq.evaluate(Seq.t, false) end
end

function Seq.toggle()
    if Seq.playing then Seq.pause() else Seq.play() end
end

-- release all actors used by the sequence and hand the camera back
function Seq.release()
    Seq.playing = false
    local s = Seq.current
    if s then
        for _, tr in ipairs(s.tracks) do
            tr.cur = nil
            detach_pose_layer(tr)
            if tr.kind == "anim" then local a = track_actor(tr); if a then a:release() end end
        end
    end
    Cam.stop()
end

Cam.time_source = function() local s = Seq.current; return Seq.t / (s and s.fps or 60) end

E.on_frame(function()
    local s = Seq.current
    if not s then return end
    if Seq.playing then
        local now = os.clock()
        local dt = math.min(now - (Seq.last_clock or now), 0.1)
        Seq.last_clock = now
        if E.frozen then dt = 0 end -- frozen world: motion isn't evaluated, so hold the timeline too
        Seq.t = Seq.t + dt * s.fps * (Seq.speed or 1)
        local a, b = play_bounds(s)
        if Seq.t >= b then
            if s.loop then
                Seq.t = a + (Seq.t - b)
                for _, tr in ipairs(s.tracks) do tr.cur = nil end
            else
                Seq.t = b
                Seq.pause()
            end
        end
    end
    Seq.evaluate(Seq.t, Seq.playing)
end)

-------------------------------------------------------------------------------
-- serialization (actors referenced by name; resolved on load)
-------------------------------------------------------------------------------
function Seq.serialize()
    local s = Seq.current
    if not s then return nil end
    local out = { name = s.name, fps = s.fps, length = s.length, loop = s.loop, range = s.range, falloff = s.falloff, audio = s.audio, shots = s.shots, tracks = {} }
    for _, tr in ipairs(s.tracks) do
        if tr.kind == "anim" then
            local clips = {}
            for _, c in ipairs(tr.clips) do
                clips[#clips + 1] = { start = c.start, dur = c.dur, path = c.path, mot = c.mot, name = c.name, endframe = c.endframe,
                    offset = c.offset, speed = c.speed, blend = c.blend, loop = c.loop, rm = c.rm, base = c.base, rm_dist = c.rm_dist }
            end
            out.tracks[#out.tracks + 1] = { kind = "anim", actor_name = tr.actor_name, layer = tr.layer, clips = clips }
        elseif tr.kind == "pose" then
            local clips = {}
            for _, c in ipairs(tr.clips) do
                clips[#clips + 1] = { start = c.start, dur = c.dur, keys = c.keys, weight = c.weight, fade_in = c.fade_in, fade_out = c.fade_out, loop = c.loop, mode = c.mode }
            end
            out.tracks[#out.tracks + 1] = { kind = "pose", actor_name = tr.actor_name, clips = clips }
        elseif tr.kind == "xform" then
            out.tracks[#out.tracks + 1] = { kind = "xform", actor_name = tr.actor_name, keys = tr.keys }
        elseif tr.kind == "cammove" then
            out.tracks[#out.tracks + 1] = { kind = "cammove", cam = tr.cam, keys = tr.keys, lookat_name = tr.lookat_name, lookat_offset = tr.lookat_offset }
        elseif tr.kind == "path" then
            out.tracks[#out.tracks + 1] = { kind = "path", actor_name = tr.actor_name, points = tr.points, start = tr.start, dur = tr.dur, speed = tr.speed, ease = tr.ease, dur_pinned = tr.dur_pinned, clip_name = tr.clip_name, clip_speed_ref = tr.clip_speed_ref }
        else
            local cuts = {}
            for _, c in ipairs(tr.cuts) do cuts[#cuts + 1] = { start = c.start, cam = c.cam } end
            out.tracks[#out.tracks + 1] = { kind = "camera", cuts = cuts }
        end
    end
    return out
end

-- resolve_actor(name) -> Actor or nil
function Seq.deserialize(data, resolve_actor)
    if not data then return end
    local s = Seq.new(data.name)
    s.fps = data.fps or 60; s.length = data.length or 600; s.loop = data.loop or false
    s.range = data.range; s.falloff = data.falloff; s.audio = data.audio; s.shots = data.shots
    for _, td in ipairs(data.tracks or {}) do
        if td.kind == "anim" then
            local a = resolve_actor(td.actor_name)
            local tr = { kind = "anim", name = td.actor_name, actor_name = td.actor_name, actor = a and a.addr or nil, layer = td.layer or 0, clips = {}, cur = nil }
            s.tracks[#s.tracks + 1] = tr
            for _, c in ipairs(td.clips or {}) do Seq.add_clip(tr, c) end
            if a then for _, c in ipairs(tr.clips) do a:load_motlist(c.path) end end
        elseif td.kind == "pose" then
            local a = resolve_actor(td.actor_name)
            local tr = { kind = "pose", name = td.actor_name .. " pose", actor_name = td.actor_name, actor = a and a.addr or nil, clips = {}, cur = nil, layer = nil }
            s.tracks[#s.tracks + 1] = tr
            for _, c in ipairs(td.clips or {}) do
                -- json arrays of keys come back as lua arrays; make sure key times are numbers
                for _, k in ipairs(c.keys or {}) do k.t = tonumber(k.t) or 0 end
                Seq.add_pose_clip(tr, c)
            end
        elseif td.kind == "xform" then
            local a = resolve_actor(td.actor_name)
            local tr = { kind = "xform", name = td.actor_name .. " move", actor_name = td.actor_name, actor = a and a.addr or nil, keys = td.keys or {}, cur = nil }
            for _, k in ipairs(tr.keys) do k.t = tonumber(k.t) or 0 end
            table.sort(tr.keys, function(x, y) return x.t < y.t end)
            s.tracks[#s.tracks + 1] = tr
        elseif td.kind == "camera" then
            local tr = Seq.add_camera_track()
            for _, c in ipairs(td.cuts or {}) do Seq.add_cut(tr, c.start, c.cam) end
        elseif td.kind == "cammove" then
            local tr = { kind = "cammove", name = "Camera " .. tostring(td.cam) .. " moves", cam = td.cam, keys = td.keys or {}, cur = nil, lookat_name = td.lookat_name, lookat_offset = td.lookat_offset }
            for _, k in ipairs(tr.keys) do k.t = tonumber(k.t) or 0 end
            table.sort(tr.keys, function(x, y) return x.t < y.t end)
            s.tracks[#s.tracks + 1] = tr
        elseif td.kind == "path" then
            local a = resolve_actor(td.actor_name)
            local tr = { kind = "path", name = td.actor_name .. " path", actor_name = td.actor_name, actor = a and a.addr or nil, points = td.points or {}, start = td.start or 0, dur = td.dur or 60,
                speed = td.speed or 1.35, ease = td.ease or 0.08, dur_pinned = td.dur_pinned, clip_name = td.clip_name, clip_speed_ref = td.clip_speed_ref, cur = nil }
            s.tracks[#s.tracks + 1] = tr
            PathMod.rebuild(tr)
        end
    end
    Seq.t = 0
    Seq.playing = false
end

return Seq
