-- Director :: pose layers — joint-rotation overrides applied after UpdateMotion (masked, weighted, keyframed)
--
-- A pose layer: { id=, name=, weight=0..1, mode="override"|"additive", joints = { [name] = Quaternion } }
-- Actors keep an ordered list `pose_layers`; Pose.apply(actor) blends them onto the engine pose.
-- Keyframed clips: { keys = { {t=frames, joints={ [name]={w,x,y,z} }}, ... } } evaluated with Pose.eval_keys.
local Log = require("Director.log")
local E = require("Director.engine")

local Pose = { next_id = 1 }

local function qnorm(q) return q:normalized() end

local function to_q(v)  -- {w,x,y,z} -> Quaternion
    return E.quat(v[1], v[2], v[3], v[4])
end
Pose.to_q = to_q

local function from_q(q) return { q.w, q.x, q.y, q.z } end
Pose.from_q = from_q

-- slerp that takes the short path
local function slerp(a, b, t)
    if a:dot(b) < 0 then b = E.quat(-b.w, -b.x, -b.y, -b.z) end
    return qnorm(a:slerp(b, t))
end
Pose.slerp = slerp

-------------------------------------------------------------------------------
-- layers on an actor
-------------------------------------------------------------------------------
function Pose.add_layer(actor, name, mode, priority)
    actor.pose_layers = actor.pose_layers or {}
    local layer = { id = Pose.next_id, name = name or ("pose " .. Pose.next_id), weight = 1.0, mode = mode or "override", joints = {}, enabled = true,
        priority = priority or 0, base = {} }  -- base[name] = engine rotation seen before this layer wrote (animated pose)
    Pose.next_id = Pose.next_id + 1
    actor.pose_layers[#actor.pose_layers + 1] = layer
    return layer
end

function Pose.remove_layer(actor, layer)
    if not actor.pose_layers then return end
    for i, l in ipairs(actor.pose_layers) do if l == layer or l.id == layer then table.remove(actor.pose_layers, i); return end end
end

function Pose.clear_layers(actor) actor.pose_layers = {} end

-- read the engine's current local rotation of a joint (call after UpdateMotion for the animated pose)
function Pose.read_local(actor, joint_name)
    local j = actor:joint(joint_name)
    if not j then return nil end
    local ok, q = pcall(function() return j:call("get_LocalRotation") end)
    return ok and q or nil
end

-- apply all enabled layers of an actor (called from Actor:apply_after_motion)
function Pose.apply(actor)
    local layers = actor.pose_layers
    if not layers or #layers == 0 then return end
    local ordered = {}
    for i, l in ipairs(layers) do ordered[i] = l end
    table.sort(ordered, function(a, b) return (a.priority or 0) < (b.priority or 0) end)
    for _, layer in ipairs(ordered) do
        if layer.enabled and layer.weight > 0.0005 and next(layer.joints) then
            for name, target in pairs(layer.joints) do
                local j = actor:joint(name)
                if j then
                    local cur = j:call("get_LocalRotation")
                    layer.base[name] = cur
                    local out
                    if layer.mode == "additive" then
                        local delta = slerp(E.quat_identity(), target, layer.weight)
                        out = qnorm(cur * delta)
                    else
                        out = (layer.weight >= 0.9995) and target or slerp(cur, target, layer.weight)
                    end
                    j:call("set_LocalRotation", out)
                end
            end
        end
    end
end

-------------------------------------------------------------------------------
-- keyframe clips
-------------------------------------------------------------------------------
-- easing (the outgoing key's `ease` shapes the segment to the next key)
--   smooth (default) · linear · hold (step) · in · out · cubic (stronger ease in/out)
-------------------------------------------------------------------------------
Pose.EASES = { "smooth", "linear", "hold", "in", "out", "cubic" }
function Pose.ease(kind, s)
    s = math.max(0, math.min(1, s))
    if kind == "linear" then return s
    elseif kind == "hold" then return 0
    elseif kind == "in" then return s * s * s
    elseif kind == "out" then local u = 1 - s; return 1 - u * u * u
    elseif kind == "cubic" then return s < 0.5 and 4 * s * s * s or 1 - (-2 * s + 2) ^ 3 / 2
    end
    return s * s * (3 - 2 * s)
end

-------------------------------------------------------------------------------
-- keys: sorted list of { t = frames (relative to clip start), joints = { [name] = {w,x,y,z} }, ease = nil|string }
function Pose.sort_keys(keys) table.sort(keys, function(a, b) return a.t < b.t end) end

-- returns table name -> Quaternion for time t (frames, relative)
function Pose.eval_keys(keys, t, loop)
    local n = #keys
    if n == 0 then return {} end
    if n == 1 then
        local out = {}
        for name, v in pairs(keys[1].joints) do out[name] = to_q(v) end
        return out
    end
    local last = keys[n].t
    if loop and last > 0 then t = t % last end
    if t <= keys[1].t then
        local out = {}
        for name, v in pairs(keys[1].joints) do out[name] = to_q(v) end
        return out
    end
    if t >= last then
        local out = {}
        for name, v in pairs(keys[n].joints) do out[name] = to_q(v) end
        return out
    end
    local k0, k1 = keys[1], keys[2]
    for i = 1, n - 1 do
        if t >= keys[i].t and t <= keys[i + 1].t then k0, k1 = keys[i], keys[i + 1]; break end
    end
    local span = k1.t - k0.t
    local s = span > 0 and (t - k0.t) / span or 1
    s = Pose.ease(k0.ease, s)
    local out = {}
    for name, v0 in pairs(k0.joints) do
        local v1 = k1.joints[name]
        out[name] = v1 and slerp(to_q(v0), to_q(v1), s) or to_q(v0)
    end
    for name, v1 in pairs(k1.joints) do
        if not out[name] then out[name] = to_q(v1) end
    end
    return out
end

-- insert/replace a key at time t with the given joints table (name -> Quaternion or {w,x,y,z})
function Pose.set_key(keys, t, joints)
    local packed = {}
    for name, q in pairs(joints) do packed[name] = (type(q) == "table") and q or from_q(q) end
    for _, k in ipairs(keys) do
        if math.abs(k.t - t) < 0.5 then
            for name, v in pairs(packed) do k.joints[name] = v end
            return k
        end
    end
    local k = { t = t, joints = packed }
    keys[#keys + 1] = k
    Pose.sort_keys(keys)
    return k
end

function Pose.remove_key(keys, t)
    for i, k in ipairs(keys) do if math.abs(k.t - t) < 0.5 then table.remove(keys, i); return true end end
    return false
end

-------------------------------------------------------------------------------
-- pose files (director/poses/<name>.json): { joints = { [name] = {w,x,y,z} } }
-------------------------------------------------------------------------------
function Pose.save_static(name, joints)
    local packed = {}
    for jn, q in pairs(joints) do packed[jn] = (type(q) == "table") and q or from_q(q) end
    local ok, err = pcall(json.dump_file, "director/poses/" .. name .. ".json", { joints = packed }, 2)
    if not ok then Log.err("pose save failed: %s", tostring(err)) end
    return ok
end

function Pose.load_static(name)
    local data = json.load_file("director/poses/" .. name .. ".json")
    if not data or not data.joints then return nil end
    local out = {}
    for jn, v in pairs(data.joints) do out[jn] = to_q(v) end
    return out
end

function Pose.list_files()
    local out = {}
    local ok, files = pcall(fs.glob, "director\\\\poses\\\\.*\\.json")
    if ok and files then for _, f in ipairs(files) do local n = f:match("([^/\\]+)%.json$"); if n then out[#out + 1] = n end end end
    table.sort(out)
    return out
end

return Pose
