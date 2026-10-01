-- Director :: Drive & Record — possess any character and walk it with WASD; optionally record into the timeline
--
--   drive_start {addr}  the character follows WASD relative to the live camera (Shift = jog); it turns toward the
--                       direction it walks and plays its own locomotion clips (stand / walk / jog) from its general set
--   R                   toggle recording while driving: the timeline plays and receives position keys (every 6 frames)
--                       plus animation clips as the locomotion state changes; a flying camera is keyed as well
--   Esc                 stop driving
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Seq = require("Director.sequence")
local Catalog = require("Director.catalog")
local RM = require("Director.rootmotion")

local Drive = { on = false, actor = nil, recording = false, state = "idle", bank = nil, clips = {}, key_every = 4, walk = 1.35, jog = 3.1, root_motion = true }

local VK = { W = 0x57, A = 0x41, S = 0x53, D = 0x44, SHIFT = 0x10, R = 0x52, ESC = 0x1B }

local function pick(mots, pats, exclude)
    for _, pat in ipairs(pats) do
        for _, m in ipairs(mots) do
            local n = m.name:lower()
            local bad = false
            for _, x in ipairs(exclude or {}) do if n:find(x, 1, true) then bad = true; break end end
            if not bad and n:find(pat, 1, true) then return m end
        end
    end
end

-- the character's everyday clip set: cast entry, else the cast catalog by character code (ch3a8z0 -> cha8)
local function general_for(a)
    if a.cast and a.cast.cast and a.cast.cast.general then return a.cast.cast.general end
    local code = a.name:match("^ch%d([a-z]%w)") and ("ch" .. a.name:match("^ch%d([a-z]%w)")) or nil
    local Cast = require("Director.cast")
    for _, c in ipairs(Cast.load()) do
        if c.id == a.name:match("^(ch%w+)_") and c.general then return c.general end
    end
    if code then
        for _, c in ipairs(Cast.load()) do if c.code == code and c.general then return c.general end end
    end
    return nil
end

function Drive.start(a)
    if not (a and a:valid()) then return false, "no actor" end
    if Drive.on then Drive.stop() end
    local general = general_for(a)
    if not general then return false, "no locomotion set for " .. a.name end
    Drive.actor = a
    Drive.on = true
    Drive.state = "idle"
    Drive.clips = {}
    Drive.frame = 0
    Drive.rec_track_clip = nil
    if a.is_player then E.set_player_frozen(true) end
    E.input_blocked = true
    if a.fsm then a:set_puppet(true) end
    if not a.root_lock then a:set_root_lock(true) end
    a:load_motlist(general, function(bank, motions)
        Drive.bank = bank
        Drive.clips.idle = pick(motions, { "stand_loop", "idle_loop", "wait_loop", "stand", "idle" }, { "hide", "crouch" })
        Drive.clips.walk = pick(motions, { "walk_f_loop", "walk_front_loop", "walk_loop" }, { "stairs", "hide", "crouch", "add", "diverse", "curve" })
        Drive.clips.jog = pick(motions, { "jog_loop_vera", "jog_loop", "run_loop", "dash_loop", "jog_f_loop" }, { "stairs", "hide", "crouch", "add", "curve" })
        if not Drive.clips.jog then Drive.clips.jog = Drive.clips.walk end
        Log.info("drive %s: idle=%s walk=%s jog=%s", a.name, Drive.clips.idle and Drive.clips.idle.name or "-", Drive.clips.walk and Drive.clips.walk.name or "-", Drive.clips.jog and Drive.clips.jog.name or "-")
        Drive.state = nil -- force the first play
    end)
    Log.info("drive on: %s", a.name)
    return true
end

function Drive.stop()
    if Drive.recording then Drive.set_recording(false) end
    local a = Drive.actor
    Drive.on = false
    E.input_blocked = false
    if a and a.is_player then E.set_player_frozen(false) end
    Drive.actor = nil
    Log.info("drive off")
end

-- recording: the timeline runs and gets keys / clips while you drive
function Drive.set_recording(on)
    on = on and true or false
    if on == Drive.recording then return end
    local a = Drive.actor
    Drive.recording = on
    if on then
        if not Seq.current then Seq.new("sequence") end
        require("Director.history").mark_break("record")
        Seq.play()
        Drive.last_key = -1
        Drive.rec_clip = nil
        if a then Seq.key_transform(a, math.floor(Seq.t)) end
        if a and Drive.state and Drive.clips[Drive.state] then Drive.open_clip(Drive.state) end
        Log.info("recording started at frame %d", math.floor(Seq.t))
    else
        Drive.close_clip()
        if a then Seq.key_transform(a, math.floor(Seq.t)) end
        Seq.pause()
        Log.info("recording stopped at frame %d", math.floor(Seq.t))
    end
end

function Drive.open_clip(state)
    local a, m = Drive.actor, Drive.clips[state]
    if not (a and m and Drive.bank) then return end
    Drive.close_clip()
    local tr = Seq.track_for_actor(a, 0, true)
    local t = math.floor(Seq.t)
    local path = Catalog.path_for_bank(Drive.bank)
    Drive.rec_clip = Seq.add_clip(tr, { start = t, path = path, mot = m.id, name = m.name, endframe = m.endframe, blend = 12, speed = 1, dur = 30, loop = true })
    Drive.rec_track = tr
end

function Drive.close_clip()
    if Drive.rec_clip then
        local t = math.floor(Seq.t)
        Drive.rec_clip.dur = math.max(1, t - Drive.rec_clip.start)
        if Drive.rec_track then Drive.rec_track.cur = nil end
        Drive.rec_clip = nil
    end
end

local last_clock = os.clock()
function Drive.update()
    if not Drive.on then return end
    local a = Drive.actor
    if not (a and a:valid()) then Drive.stop(); return end
    local now = os.clock(); local dt = math.min(now - last_clock, 0.1); last_clock = now
    if not E.game_focused or Cam.fly.on then
        if Drive.recording then
            -- keep recording a flying camera even while the character stands still
            local t = math.floor(Seq.t)
            if Cam.fly.on and Cam.active and t - (Drive.last_key or -99) >= Drive.key_every then Seq.key_camera(Cam.active, t); Drive.last_key = t end
        end
        return
    end
    if E.key_pressed(VK.ESC) then Drive.stop(); return end
    if E.key_pressed(VK.R) then Drive.set_recording(not Drive.recording) end
    -- movement relative to the view
    local pose = Cam.game_pose()
    local f, r = Vector3f.new(0, 0, 1), Vector3f.new(1, 0, 0)
    if pose then
        f, r = E.basis(E.quat(pose.rot[1], pose.rot[2], pose.rot[3], pose.rot[4]))
        f.y = 0; r.y = 0
        if f:length() > 0.01 then f = f:normalized() end
        if r:length() > 0.01 then r = r:normalized() end
    end
    local d = Vector3f.new(0, 0, 0)
    if E.key_down(VK.W) then d = d + f end
    if E.key_down(VK.S) then d = d - f end
    if E.key_down(VK.D) then d = d + r end
    if E.key_down(VK.A) then d = d - r end
    local moving = d:length() > 0.01
    local state = moving and (E.key_down(VK.SHIFT) and "jog" or "walk") or "idle"
    if state ~= Drive.state and Drive.bank and Drive.clips[state] then
        Drive.state = state
        a:play(Drive.bank, Drive.clips[state].id, { layer = 0, blend = 12, speed = 1 })
        if Drive.recording then Drive.open_clip(state) end
    end
    local p = E.v3(a.xform:call("get_Position"))
    local oldp = Vector3f.new(p.x, p.y, p.z)
    local q = a.xform:call("get_Rotation")
    local feet = Drive.root_motion and RM.step(a) or nil -- measured every frame so the tracker stays primed
    if moving then
        d = d:normalized()
        -- turn toward the direction of travel (character forward is +Z)
        local want = math.atan(d.x, d.z)
        local bz = E.basis(q); local have = math.atan(-bz.x, -bz.z) -- E.basis returns the -Z axis; characters face +Z
        local diff = (want - have + math.pi) % (2 * math.pi) - math.pi
        local max_step = math.rad(540 * dt)
        local yaw = have + math.max(-max_step, math.min(max_step, diff))
        local h = yaw * 0.5
        q = E.quat(math.cos(h), 0, math.sin(h), 0)
        if feet and feet:length() > 1e-5 then
            -- travel exactly as far as the planted foot slid: no skating at any speed
            local w = RM.qrot(q, feet)
            p = Vector3f.new(p.x + w.x, p.y, p.z + w.z)
        else
            local speed = (state == "jog") and Drive.jog or Drive.walk
            p = Vector3f.new(p.x + d.x * speed * dt, p.y, p.z + d.z * speed * dt)
        end
    end
    if moving then p = require("Director.extras").resolve_motion(oldp, p) or p end
    Drive.gframe = (Drive.gframe or 0) + 1
    if moving and Drive.gframe % 3 == 0 then
        local gy = require("Director.extras").ground_y(p.x, p.y, p.z, 1.0, 2.5)
        if gy and math.abs(gy - p.y) < 1.2 then p = Vector3f.new(p.x, p.y + (gy - p.y) * 0.5, p.z) end
    end
    a:set_root_pose(p, q)
    if Drive.recording then
        local s = Seq.current
        if s and Seq.t > s.length - 120 then s.length = s.length + 600 end -- keep the tape rolling
        local t = math.floor(Seq.t)
        if t - (Drive.last_key or -99) >= Drive.key_every then
            Seq.key_transform(a, t, p, q)
            if Cam.fly.on and Cam.active then Seq.key_camera(Cam.active, t) end
            Drive.last_key = t
        end
        if Drive.rec_clip then Drive.rec_clip.dur = math.max(1, t - Drive.rec_clip.start + 1) end
    end
end

function Drive.state_export()
    return { on = Drive.on, actor = Drive.actor and Drive.actor.addr or nil, name = Drive.actor and (Drive.actor.display_name or Drive.actor.name) or nil,
        recording = Drive.recording, state = Drive.state }
end

return Drive
