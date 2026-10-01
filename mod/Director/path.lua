-- Director :: paths — draw waypoints on the floor, the character walks the curve from the first to the last
--
-- A path track: { kind="path", actor=addr, actor_name=, points={{x,y,z},...}, start=frame, dur=frames, speed=m/s,
--                 ease=0..1 (fraction of the walk spent accelerating / braking), clip_id=<anim clip id on layer 0>, ... }
-- The curve is a Catmull-Rom spline through the points (rounded corners); an arc-length table makes the character
-- move at a constant ground speed. Speed comes from the walk clip's measured travel (root-motion bake) so the feet
-- plant; `dur` is derived from length / speed, or set by hand (then the clip's playback speed scales to match).
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")

local Path = { drawing = nil --[[ { track = tr } while points are being placed in edit mode ]] }

local SAMPLES = 16 -- curve samples per segment for the arc-length table

local function v(p) return Vector3f.new(p[1], p[2], p[3]) end

-- Catmull-Rom point on segment i (points i, i+1) at u in [0,1]
local function cr(p0, p1, p2, p3, u)
    local u2, u3 = u * u, u * u * u
    local function c(a, b, cc, d) return 0.5 * ((2 * b) + (-a + cc) * u + (2 * a - 5 * b + 4 * cc - d) * u2 + (-a + 3 * b - 3 * cc + d) * u3) end
    return Vector3f.new(c(p0.x, p1.x, p2.x, p3.x), c(p0.y, p1.y, p2.y, p3.y), c(p0.z, p1.z, p2.z, p3.z))
end

-- (re)build the arc-length table: tr.samples = { {d=, pos=Vector3f, tan=Vector3f}, ... }, tr.length
function Path.rebuild(tr)
    local pts = tr.points or {}
    tr.samples, tr.length = {}, 0
    if #pts == 0 then return end
    if #pts == 1 then tr.samples[1] = { d = 0, pos = v(pts[1]), tan = Vector3f.new(0, 0, 1) }; return end
    local P = {}
    for i, p in ipairs(pts) do P[i] = v(p) end
    local d = 0
    local prev
    for i = 1, #P - 1 do
        local p0, p1, p2, p3 = P[math.max(1, i - 1)], P[i], P[i + 1], P[math.min(#P, i + 2)]
        local n = (i == #P - 1) and SAMPLES or SAMPLES - 1
        for s = 0, n do
            local u = s / SAMPLES
            local pos = cr(p0, p1, p2, p3, u)
            if prev then d = d + (pos - prev):length() end
            tr.samples[#tr.samples + 1] = { d = d, pos = pos }
            prev = pos
        end
    end
    tr.length = d
    -- tangents (forward differences, last = previous)
    for i, s in ipairs(tr.samples) do
        local nxt = tr.samples[i + 1] or s
        local prv = tr.samples[i - 1] or s
        local t = nxt.pos - prv.pos; t.y = 0
        s.tan = (t:length() > 1e-5) and t:normalized() or (tr.samples[i - 1] and tr.samples[i - 1].tan or Vector3f.new(0, 0, 1))
    end
end

-- position + tangent at ground distance d along the path
function Path.at(tr, d)
    local S = tr.samples
    if not S or #S == 0 then return nil end
    if d <= 0 then return S[1].pos, S[1].tan end
    if d >= tr.length then return S[#S].pos, S[#S].tan end
    local lo, hi = 1, #S
    while hi - lo > 1 do local mid = (lo + hi) // 2; if S[mid].d <= d then lo = mid else hi = mid end end
    local a, b = S[lo], S[hi]
    local u = (b.d > a.d) and (d - a.d) / (b.d - a.d) or 0
    local pos = a.pos + (b.pos - a.pos) * u
    local tan = (a.tan * (1 - u) + b.tan * u); if tan:length() > 1e-5 then tan = tan:normalized() else tan = a.tan end
    return pos, tan
end

-- distance travelled at frame t: constant ground speed with a short ease in / out (fraction `ease` of the walk each)
local function S(u) return u * u * u - u * u * u * u * 0.5 end  -- integral of smoothstep u^2(3-2u) from 0 to u
function Path.distance_at(tr, t)
    local dur = math.max(1, tr.dur or 1)
    local x = math.max(0, math.min(1, (t - tr.start) / dur))
    local e = math.max(0, math.min(0.45, tr.ease or 0.08))
    if e <= 0 then return x * tr.length, x end
    local area = 1 - e -- integral of the speed profile over [0,1]
    local s
    if x < e then s = e * S(x / e)
    elseif x <= 1 - e then s = e * 0.5 + (x - e)
    else s = e * 0.5 + (1 - 2 * e) + e * (0.5 - S((1 - x) / e)) end
    local frac = math.max(0, math.min(1, s / area))
    return frac * tr.length, frac
end

function Path.duration_for(tr)
    local speed = tr.speed or 1.35
    if speed <= 0.01 then speed = 1.35 end
    return math.max(1, math.floor(tr.length / speed * 60 + 0.5))
end

-- keep the actor's walk clip in step with the path (dur / speed) — the clip lives on the anim track (layer 0)
function Path.sync_clip(tr, Seq)
    local a = tr.actor and Actor.get(tr.actor); if not a then return end
    local atr = Seq.track_for_actor(a, 0, false)
    if not atr then return end
    for _, c in ipairs(atr.clips) do
        if c.id == tr.clip_id then
            c.start = tr.start
            c.dur = tr.dur
            c.loop = true
            -- footsteps match the ground: playback speed = actual ground speed / the clip's natural travel speed
            if tr.clip_speed_ref and tr.clip_speed_ref > 0.05 and tr.dur > 0 then
                local ground = tr.length / (tr.dur / 60)
                c.speed = math.max(0.3, math.min(3, ground / tr.clip_speed_ref))
            end
            atr.cur = nil
            return
        end
    end
end

-- per frame: place the character on the curve (called from Seq.evaluate after the xform tracks)
function Path.evaluate(tr, t, ground_y, resolve_motion)
    local a = tr.actor and Actor.get(tr.actor)
    if not (a and a:valid()) or a.detached then return end
    if not tr.samples then Path.rebuild(tr) end
    if not tr.samples or #tr.samples == 0 then return end
    if t < tr.start - 0.5 and not tr.hold_before then return end
    local d, frac = Path.distance_at(tr, t)
    local pos, tan = Path.at(tr, d)
    if not pos then return end
    local y = pos.y
    if ground_y then
        local gy = ground_y(pos.x, pos.y, pos.z)
        if gy and math.abs(gy - pos.y) < 1.5 then y = gy end
    end
    if resolve_motion then
        local cur = E.v3(a.xform:call("get_Position"))
        pos = resolve_motion(cur, Vector3f.new(pos.x, y, pos.z)) or pos
        y = pos.y
    end
    if not a.root_lock then a:set_root_lock(true) end
    local yaw = math.atan(tan.x, tan.z) -- characters face +Z
    local h = yaw * 0.5
    a:set_root_pose(Vector3f.new(pos.x, y, pos.z), E.quat(math.cos(h), 0, math.sin(h), 0))
    a.path_serial = tr.serial_now
end

-- drawing: the curve and its points, in the picture
function Path.draw(tr, selected)
    if not tr.samples then Path.rebuild(tr) end
    local col = selected and 0xFF5FD9A6 or 0x995FD9A6
    local prev
    for _, s in ipairs(tr.samples or {}) do
        local sp = draw.world_to_screen(Vector3f.new(s.pos.x, s.pos.y + 0.03, s.pos.z))
        if sp and prev then draw.line(prev.x, prev.y, sp.x, sp.y, col) end
        prev = sp
    end
    for i, p in ipairs(tr.points or {}) do
        local sp = draw.world_to_screen(Vector3f.new(p[1], p[2] + 0.03, p[3]))
        if sp then
            draw.filled_circle(sp.x, sp.y, i == 1 and 6 or 4, col)
            if i == 1 or i == #tr.points then draw.text(i == 1 and "start" or "end", sp.x + 8, sp.y - 8, col) end
        end
    end
end

return Path
