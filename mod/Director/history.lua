-- Director :: undo / redo — whole-document snapshots (sequence, cameras, actor placement, working poses)
--
--   History.mark(label)  take a snapshot before a change; consecutive marks with the same label inside
--                        `coalesce_s` seconds are merged, so a drag that streams 200 commands costs one undo step
--   History.undo() / History.redo()
-- Spawning / destroying cast members is not undone (it is asynchronous); everything else is.
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Seq = require("Director.sequence")

local H = { past = {}, future = {}, max = 60, coalesce_s = 0.9, last_mark = nil, working = nil --[[ function(actor) -> work, layer (set by the bridge) ]] }

local function deep(v)
    if type(v) ~= "table" then return v end
    local out = {}
    for k, x in pairs(v) do out[k] = deep(x) end
    return out
end

-------------------------------------------------------------------------------
-- snapshot
-------------------------------------------------------------------------------
local function snap_actor(a)
    local p = a.xform:call("get_Position")
    local r = a.xform:call("get_Rotation")
    local scale = a.xform:call("get_LocalScale")
    local joints, joint, weight = nil, nil, nil
    if a.work and a.work.layer then
        joints = {}
        for n, q in pairs(a.work.layer.joints) do joints[n] = q end
        joint = a.work.joint
        weight = a.work.layer.weight
    end
    return { addr = a.addr, pos = Vector3f.new(p.x, p.y, p.z), rot = E.quat(r.w, r.x, r.y, r.z), scale = Vector3f.new(scale.x, scale.y, scale.z), locked = a.root_lock ~= nil, puppet = a.puppet,
        joints = joints, joint = joint, weight = weight }
end

local function snapshot(label)
    local actors = {}
    for _, a in ipairs(Actor.all()) do
        if a:valid() then local ok, s = pcall(snap_actor, a); if ok then actors[#actors + 1] = s end end
    end
    return { label = label, time = os.clock(), seq = Seq.snapshot(), cams = deep(Cam.cams), actors = actors }
end

local function restore(s)
    Seq.restore(deep(s.seq))
    Cam.cams = deep(s.cams)
    Cam.cams[0] = Cam.work   -- the work camera is not part of the film: undo never moves it
    if Cam.active and not Cam.cams[Cam.active] then Cam.stop() end
    for _, sa in ipairs(s.actors) do
        local a = Actor.get(sa.addr)
        if a and a:valid() then
            pcall(function()
                if sa.locked and not a.root_lock then a:set_root_lock(true) end
                if not sa.locked and a.root_lock then a.root_lock = nil end
                if sa.locked or a.spawned or a.kind == "object" then a:set_root_pose(sa.pos, sa.rot) end
                if sa.scale then a.xform:call("set_LocalScale", sa.scale) end
                if sa.joints and H.working then
                    local w, layer = H.working(a)
                    local j = {}
                    for n, q in pairs(sa.joints) do j[n] = q end
                    layer.joints = j; w.saved_joints = j
                    w.joint = sa.joint
                    if sa.weight then layer.weight = sa.weight end
                end
            end)
        end
    end
end

-------------------------------------------------------------------------------
-- api
-------------------------------------------------------------------------------
function H.mark(label)
    local now = os.clock()
    local top = H.past[#H.past]
    if top and H.last_mark == label and now - top.time < H.coalesce_s then top.time = now; return end
    local ok, s = pcall(snapshot, label)
    if not ok then Log.err("history snapshot failed: %s", tostring(s)); return end
    H.past[#H.past + 1] = s
    if #H.past > H.max then table.remove(H.past, 1) end
    H.future = {}
    H.last_mark = label
end

-- a mark that is never merged with the previous one (start of a drag / recording)
function H.mark_break(label)
    H.last_mark = nil
    H.mark(label)
    H.last_mark = nil
end

function H.undo()
    local s = table.remove(H.past)
    if not s then return false end
    local ok, cur = pcall(snapshot, s.label)
    if ok then H.future[#H.future + 1] = cur end
    local okr, err = pcall(restore, s)
    if not okr then Log.err("undo failed: %s", tostring(err)) end
    H.last_mark = nil
    Log.info("undo: %s", tostring(s.label))
    return okr
end

function H.redo()
    local s = table.remove(H.future)
    if not s then return false end
    local ok, cur = pcall(snapshot, s.label)
    if ok then H.past[#H.past + 1] = cur end
    local okr, err = pcall(restore, s)
    if not okr then Log.err("redo failed: %s", tostring(err)) end
    H.last_mark = nil
    Log.info("redo: %s", tostring(s.label))
    return okr
end

function H.clear() H.past, H.future, H.last_mark = {}, {}, nil end

function H.state()
    local top = H.past[#H.past]
    local nxt = H.future[#H.future]
    return { undo = #H.past, redo = #H.future, last = top and top.label or nil, next = nxt and nxt.label or nil }
end

return H
