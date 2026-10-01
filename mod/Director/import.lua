-- Director :: external animations — rig capture for the retargeter and import of retargeted clips
--
--   Import.export_rig(actor)            writes director/rigs/<code>.json: every joint's rest world pose + parent
--   Import.apply(actor, file, start)    reads director/import/<code>/<name>.json (from studio/retarget.py) and lays a
--                                       pose clip (dense keys) + root travel keys onto the actor's tracks at `start`
local Log = require("Director.log")
local E = require("Director.engine")
local Seq = require("Director.sequence")
local Pose = require("Director.pose")

local Import = {}

function Import.rig_code(a)
    if a.cast and a.cast.cast and a.cast.cast.id then return a.cast.cast.id end
    return a.name:match("^(ch%w+)") or a.name:gsub("[^%w]", "_")
end

local function qmul_local(parent_world, local_q) return (parent_world * local_q):normalized() end

-- rest world pose per joint. Prefers the bind pose (get_BaseLocal*), falls back to the current pose.
function Import.capture_rig(a)
    local names = a:joint_names()
    local joints, order = {}, {}
    local have_base = false
    for _, n in ipairs(names) do
        local j = a:joint(n)
        if j then
            local parent = nil
            pcall(function() local p = j:call("get_Parent"); if p then parent = p:call("get_Name") end end)
            local lp, lr
            pcall(function() lp = j:call("get_BaseLocalPosition"); lr = j:call("get_BaseLocalRotation") end)
            if lp and lr then have_base = true end
            local wp, wr
            pcall(function() wp = j:call("get_Position"); wr = j:call("get_Rotation") end)
            joints[n] = { parent = parent, lp = lp, lr = lr, wp = wp, wr = wr }
            order[#order + 1] = n
        end
    end
    -- compose the bind pose through the hierarchy (root joints: base local == world relative to the transform)
    local xp = a.xform:call("get_Position"); local xr = a.xform:call("get_Rotation")
    local xq = E.quat(xr.w, xr.x, xr.y, xr.z)
    local xinv = E.quat(xq.w, -xq.x, -xq.y, -xq.z)
    local out = {}
    local function world_rest(n, depth)
        local jn = joints[n]
        if jn.world then return jn.world end
        if have_base and jn.lp and jn.lr and depth < 64 then
            local lq = E.quat(jn.lr.w, jn.lr.x, jn.lr.y, jn.lr.z)
            local lpv = Vector3f.new(jn.lp.x, jn.lp.y, jn.lp.z)
            if jn.parent and joints[jn.parent] then
                local pw = world_rest(jn.parent, depth + 1)
                local m = pw.rot:to_mat4()
                local off = Vector3f.new(m[0].x * lpv.x + m[1].x * lpv.y + m[2].x * lpv.z, m[0].y * lpv.x + m[1].y * lpv.y + m[2].y * lpv.z, m[0].z * lpv.x + m[1].z * lpv.y + m[2].z * lpv.z)
                jn.world = { pos = pw.pos + off, rot = qmul_local(pw.rot, lq) }
            else
                jn.world = { pos = lpv, rot = lq } -- relative to the character transform (origin, facing +Z)
            end
        else
            -- current pose expressed relative to the character transform
            local d = Vector3f.new(jn.wp.x - xp.x, jn.wp.y - xp.y, jn.wp.z - xp.z)
            local m = xinv:to_mat4()
            local lpos = Vector3f.new(m[0].x * d.x + m[1].x * d.y + m[2].x * d.z, m[0].y * d.x + m[1].y * d.y + m[2].y * d.z, m[0].z * d.x + m[1].z * d.y + m[2].z * d.z)
            jn.world = { pos = lpos, rot = (xinv * E.quat(jn.wr.w, jn.wr.x, jn.wr.y, jn.wr.z)):normalized() }
        end
        return jn.world
    end
    for _, n in ipairs(order) do
        local w = world_rest(n, 0)
        out[n] = { parent = joints[n].parent, pos = { w.pos.x, w.pos.y, w.pos.z }, rot = { w.rot.w, w.rot.x, w.rot.y, w.rot.z } }
    end
    return { code = Import.rig_code(a), name = a.name, base_pose = have_base, joints = out }
end

function Import.export_rig(a)
    local ok, rig = pcall(Import.capture_rig, a)
    if not ok then Log.err("rig capture failed: %s", tostring(rig)); return nil end
    local file = "director/rigs/" .. rig.code .. ".json"
    local okw, err = pcall(json.dump_file, file, rig, 0)
    if not okw then Log.err("rig write failed: %s", tostring(err)); return nil end
    local n = 0; for _ in pairs(rig.joints) do n = n + 1 end
    Log.info("rig exported: %s (%d joints, bind pose %s)", file, n, tostring(rig.base_pose))
    return file
end

-- lay the retargeted clip onto the timeline
function Import.apply(a, file, start, opts)
    opts = opts or {}
    local data = json.load_file(file)
    if not (data and data.keys) then return false, "no clip data in " .. tostring(file) end
    start = math.floor(start or Seq.t)
    local tr = Seq.pose_track_for_actor(a, true)
    local keys = {}
    for _, k in ipairs(data.keys) do
        local packed = {}
        for n, v in pairs(k.joints or {}) do packed[n] = { v[1], v[2], v[3], v[4] } end
        keys[#keys + 1] = { t = math.floor(k.t), joints = packed, ease = "linear" }
    end
    Pose.sort_keys(keys)
    local dur = math.max(1, (keys[#keys] and keys[#keys].t or 0) + 1)
    local clip = Seq.add_pose_clip(tr, { start = start, dur = dur, keys = keys, mode = "override", fade_in = 6, fade_out = 6, loop = opts.loop or false,
        name = (file:match("([^/\\]+)%.json$") or "import"), imported = true })
    -- root travel: relative to where the character stands and faces now
    local nroot = 0
    if data.root and #data.root > 1 and opts.root ~= false then
        local p = a.xform:call("get_Position"); local r = a.xform:call("get_Rotation")
        local _, yaw0 = E.euler_deg(r)
        local src0 = data.root[1].yaw or 0
        local turn = math.rad(yaw0 - src0)
        local cs, sn = math.cos(turn), math.sin(turn)
        local every = opts.root_every or 4
        for i, k in ipairs(data.root) do
            if (i - 1) % every == 0 or i == #data.root then
                local dx, dz = k.dx or 0, k.dz or 0
                local wx, wz = dx * cs + dz * sn, -dx * sn + dz * cs
                local yaw = yaw0 + ((k.yaw or src0) - src0)
                local _, key = Seq.key_transform(a, start + math.floor(k.t), Vector3f.new(p.x + wx, p.y, p.z + wz), E.quat_from_euler_deg(0, yaw, 0))
                key.ease = "linear"
                nroot = nroot + 1
            end
        end
    end
    tr.cur = nil
    Log.info("imported %s: %d pose keys, %d travel keys, %d frames", tostring(file), #keys, nroot, dur)
    return clip
end

-- imported clips available for a rig code
function Import.list(code)
    local out = {}
    local ok, files = pcall(fs.glob, "director\\\\import\\\\" .. code .. "\\\\.*\\.json")
    if ok and files then for _, f in ipairs(files) do local n = f:match("([^/\\]+)%.json$"); if n then out[#out + 1] = n end end end
    table.sort(out)
    return out
end

return Import
