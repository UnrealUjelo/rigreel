-- Director :: root motion from the feet — no more skating
--
-- Locomotion clips play "in place": the planted foot slides backwards under the body. Measuring that slide in the
-- character's local space each frame gives the distance the body should have travelled, independent of what the
-- engine does with the clip's root joint. Two uses:
--   RM.step(actor)                 live: returns this frame's local displacement (drive mode moves the body by it)
--   RM.bake(actor, bank, mot, ...) offline: plays a clip once and records the cumulative displacement per frame, so a
--                                  timeline clip can place the character deterministically (scrub-safe, loop-aware)
local Log = require("Director.log")
local E = require("Director.engine")

local RM = { baking = nil }

local function qinv(q) return E.quat(q.w, -q.x, -q.y, -q.z) end
local function qrot(q, v)
    local m = q:to_mat4()
    return Vector3f.new(m[0].x * v.x + m[1].x * v.y + m[2].x * v.z, m[0].y * v.x + m[1].y * v.y + m[2].y * v.z, m[0].z * v.x + m[1].z * v.y + m[2].z * v.z)
end
RM.qrot = qrot

local function feet_local(a)
    local lf, rf = a:joint("L_Foot"), a:joint("R_Foot")
    if not (lf and rf) then return nil end
    local p = a.xform:call("get_Position"); local r = a.xform:call("get_Rotation")
    local inv = qinv(E.quat(r.w, r.x, r.y, r.z))
    local function loc(j) local jp = j:call("get_Position"); return qrot(inv, Vector3f.new(jp.x - p.x, jp.y - p.y, jp.z - p.z)) end
    return loc(lf), loc(rf)
end

-- local-space displacement since the last call (x, z; y always 0). First call primes and returns zero.
function RM.step(a)
    local L, R = feet_local(a)
    local zero = Vector3f.new(0, 0, 0)
    if not L then return zero end
    local st = a.rm_state
    if not st then a.rm_state = { L = L, R = R, planted = "L" }; return zero end
    local dL, dR = L - st.L, R - st.R
    -- planted foot: the lower one; when they are level, the slower one
    local planted
    if L.y < R.y - 0.015 then planted = "L" elseif R.y < L.y - 0.015 then planted = "R"
    else planted = (dL:length() <= dR:length()) and "L" or "R" end
    local d = (planted == "L") and dL or dR
    st.L, st.R, st.planted = L, R, planted
    local out = Vector3f.new(-d.x, 0, -d.z)
    if out:length() > 0.25 then return zero end -- loop wrap / motion change: not travel
    return out
end

function RM.reset(a) a.rm_state = nil end

-------------------------------------------------------------------------------
-- bake: cumulative displacement per clip frame → { n = endframe, x = {...}, z = {...} } (index = frame + 1)
-------------------------------------------------------------------------------
function RM.bake(a, bank, mot, endframe, layer, cb)
    if RM.baking then return false, "already baking" end
    endframe = math.max(2, math.floor(endframe or 60))
    local was_paused = a.paused
    a:set_paused(false)
    a:play(bank, mot, { layer = layer or 0, blend = 0, start = 0, speed = 1 })
    RM.reset(a)
    RM.baking = { a = a, bank = bank, mot = mot, layer = layer or 0, n = endframe, cum = {}, cx = 0, cz = 0, last_f = -1, frames = 0, cb = cb, was_paused = was_paused, started = os.clock() }
    Log.info("root motion bake: %s mot %s (%d frames)", a.name, tostring(mot), endframe)
    return true
end

local function finish(ok)
    local b = RM.baking; RM.baking = nil
    if not b then return end
    local curve = { n = b.n, x = {}, z = {} }
    -- fill every frame (interpolate gaps between sampled frames)
    local lastx, lastz = 0, 0
    local prev_i = 0
    for i = 0, b.n do
        local c = b.cum[i]
        if c then
            for j = prev_i + 1, i - 1 do
                local u = (j - prev_i) / (i - prev_i)
                curve.x[j + 1] = lastx + (c[1] - lastx) * u; curve.z[j + 1] = lastz + (c[2] - lastz) * u
            end
            curve.x[i + 1] = c[1]; curve.z[i + 1] = c[2]
            lastx, lastz, prev_i = c[1], c[2], i
        end
    end
    for j = prev_i + 1, b.n do curve.x[j + 1] = lastx; curve.z[j + 1] = lastz end
    curve.x[1] = curve.x[1] or 0; curve.z[1] = curve.z[1] or 0
    if b.was_paused then b.a:set_paused(true) end
    local dist = math.sqrt(lastx * lastx + lastz * lastz)
    Log.info("root motion bake done: %.2f m over %d frames (%s)", dist, b.n, ok and "ok" or "timeout")
    if b.cb then pcall(b.cb, ok and curve or nil, dist) end
end

E.on_frame(function()
    local b = RM.baking
    if not b then return end
    if not b.a:valid() then finish(false); return end
    local li = b.a:layer_info(b.layer)
    local f = li and li.frame or 0
    if li and li.mot ~= b.mot then
        if os.clock() - b.started > 2 then finish(false) end
        return
    end
    local d = RM.step(b.a)
    local fi = math.floor(f)
    if fi < b.last_f then -- wrapped (loop) or restarted: done
        finish(true); return
    end
    b.cx = b.cx + d.x; b.cz = b.cz + d.z
    b.cum[fi] = { b.cx, b.cz }
    b.last_f = fi
    b.frames = b.frames + 1
    if fi >= b.n - 1 or os.clock() - b.started > 30 then finish(true) end
end)

-- cumulative displacement (local x, z) at clip frame f (loop-aware)
function RM.at(curve, f, loop)
    if not curve or not curve.n or curve.n < 1 then return 0, 0 end
    local n = curve.n
    local loops = 0
    if loop and f >= n then loops = math.floor(f / n); f = f - loops * n end
    f = math.max(0, math.min(f, n))
    local i = math.floor(f)
    local u = f - i
    local x0, z0 = curve.x[i + 1] or 0, curve.z[i + 1] or 0
    local x1, z1 = curve.x[math.min(n, i + 1) + 1] or x0, curve.z[math.min(n, i + 1) + 1] or z0
    local ex, ez = curve.x[n + 1] or 0, curve.z[n + 1] or 0
    return loops * ex + x0 + (x1 - x0) * u, loops * ez + z0 + (z1 - z0) * u
end

return RM
