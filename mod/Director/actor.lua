-- Director :: Actor — any GameObject with a via.motion.Motion component
local Log = require("Director.log")
local E = require("Director.engine")
local Catalog = require("Director.catalog")
local Pose = require("Director.pose")

local CHANGE_MOTION = "changeMotion(System.UInt32, System.UInt32, System.Single, System.Single, via.motion.InterpolationMode, via.motion.InterpolationCurve)"

local Actor = {}
Actor.__index = Actor
Actor.registry = {}     -- address -> Actor
Actor.order = {}        -- addresses in creation order (stable UI ordering)

-------------------------------------------------------------------------------
-- construction / lookup
-------------------------------------------------------------------------------
function Actor.wrap(go)
    if not E.valid(go) then return nil end
    local addr = go:get_address()
    local a = Actor.registry[addr]
    if a then return a end
    local motion = E.component(go, "via.motion.Motion")   -- may be nil for props/objects
    if not motion and not E.transform(go) then return nil end
    a = setmetatable({
        kind = motion and "character" or "object",
        addr = addr,
        go = go,
        name = E.go_name(go),
        xform = E.transform(go),
        motion = motion,
        fsm = E.component(go, "via.motion.MotionFsm2"),
        banks = {},          -- bank_id -> { path=, ready=bool, motions=list|nil, frames=int }
        puppet = false,
        puppet_orig = nil,
        paused = false,
        root_lock = nil,     -- { pos=Vector3f, rot=Quaternion } when locked
        is_player = false,
        overrides = {},      -- legacy: joint name -> Quaternion (applied after UpdateMotion at full weight)
        override_joints = {},-- joint name -> REJoint (cache)
        pose_layers = {},    -- Pose layers (see Director/pose.lua)
    }, Actor)
    a.is_player = (E.player_body() == go)
    Actor.registry[addr] = a
    Actor.order[#Actor.order + 1] = addr
    Log.info("actor registered: %s%s", a.name, a.is_player and " (player)" or "")
    return a
end

function Actor.get(addr) return Actor.registry[addr] end

function Actor.get_by_name(name)
    for _, addr in ipairs(Actor.order) do local a = Actor.registry[addr]; if a and a.name == name and a:valid() then return a end end
end

function Actor.all(include_internal)
    local out = {}
    for _, addr in ipairs(Actor.order) do
        local a = Actor.registry[addr]
        if a and a:valid() and (include_internal or not a.internal) then out[#out + 1] = a end
    end
    return out
end

function Actor:valid()
    if not E.valid(self.go) then return false end
    local ok = pcall(function() return self.go:call("get_Name") end)
    return ok
end

function Actor:forget()
    self:release()
    Actor.registry[self.addr] = nil
    for i, addr in ipairs(Actor.order) do
        if addr == self.addr then table.remove(Actor.order, i); break end
    end
end

-- scan the scene for candidate GameObjects (Motion component). Returns sorted list of
-- { go=, name=, dist=, has_fsm=, is_player=, registered= }
function Actor.scan(filter)
    local body = E.player_body()
    local origin = body and E.transform(body) and E.transform(body):call("get_Position")
    local out, seen = {}, {}
    for _, motion in ipairs(E.find_components("via.motion.Motion")) do
        local ok, go = pcall(function() return motion:call("get_GameObject") end)
        if ok and go and not seen[go:get_address()] then
            seen[go:get_address()] = true
            local name = E.go_name(go)
            if not name:match("^Director_Preview") and (not filter or filter == "" or name:lower():find(filter:lower(), 1, true)) then
                local xf = E.transform(go)
                local d = -1
                if origin and xf then
                    local okp, p = pcall(function() return xf:call("get_Position") end)
                    if okp and p then d = E.dist(p, origin) end
                end
                out[#out + 1] = {
                    go = go, name = name, dist = d,
                    has_fsm = E.component(go, "via.motion.MotionFsm2") ~= nil,
                    is_player = (go == body),
                    registered = Actor.registry[go:get_address()] ~= nil,
                }
            end
        end
    end
    table.sort(out, function(a, b)
        if a.is_player ~= b.is_player then return a.is_player end
        if a.has_fsm ~= b.has_fsm then return a.has_fsm end
        return (a.dist < 0 and 1e9 or a.dist) < (b.dist < 0 and 1e9 or b.dist)
    end)
    return out
end

-- nearby props/objects: GameObjects with a via.render.Mesh but no Motion. Returns { go=, name=, dist= } sorted by distance.
function Actor.scan_objects(radius, limit)
    radius = radius or 25; limit = limit or 200
    local body = E.player_body()
    local origin = body and E.transform(body) and E.v3(E.transform(body):call("get_Position"))
    if not origin then return {} end
    local out, seen = {}, {}
    local r2 = radius * radius
    for _, mesh in ipairs(E.find_components("via.render.Mesh")) do
        local ok, go = pcall(function() return mesh:call("get_GameObject") end)
        if ok and go then
            local addr = go:get_address()
            if not seen[addr] then
                seen[addr] = true
                local xf = E.transform(go)
                if xf then
                    local okp, p = pcall(function() return xf:call("get_Position") end)
                    if okp and p then
                        local dx, dy, dz = p.x - origin.x, p.y - origin.y, p.z - origin.z
                        local d2 = dx * dx + dy * dy + dz * dz
                        local name = E.go_name(go)
                        if d2 < r2 and not E.component(go, "via.motion.Motion") and not name:match("^Director_Preview") and name ~= "Target" and name ~= "Line" then
                            out[#out + 1] = { go = go, name = E.go_name(go), dist = math.sqrt(d2), registered = Actor.registry[addr] ~= nil }
                        end
                    end
                end
            end
        end
    end
    table.sort(out, function(a, b) return a.dist < b.dist end)
    while #out > limit do table.remove(out) end
    return out
end

-------------------------------------------------------------------------------
-- motion banks (async)
-------------------------------------------------------------------------------
local function find_dynamic_bank(motion, bank_id)
    local count = motion:call("getDynamicMotionBankCount")
    for i = 0, count - 1 do
        local db = motion:call("getDynamicMotionBank", i)
        if db and db:call("get_BankID") == bank_id then return db, i end
    end
    return nil, count
end

local function head_motlist(path)
    if not path then return false end
    local ce = Catalog.entry(path)
    return (ce and ce.g and ce.g:match("^facial")) or path:lower():match("_10%.motlist$")
end

local function wrong_rig(a, path)
    if not (a.cast and path) then return false end
    local ce = Catalog.entry(path)
    local owner = ce and ce.c and ce.c:match("^(cha%d)$")
        or path:lower():match("/(cha%d)%d%d_%d%d%.motlist$")
    local code = a.cast.cast and a.cast.cast.code
    return owner and code and owner ~= code
end

-- Ensure a motlist is attached to this actor under its catalog bank id. cb(bank_id, motions) when ready.
function Actor:load_motlist(path, cb)
    if not self.motion then Log.warn("%s has no animation component", self.name); return nil end
    if wrong_rig(self, path) then Log.warn("%s: animation belongs to another character (%s)", self.name, path); return nil end
    if self.cast and head_motlist(path) then return require("Director.cast").load_facial(self, path, cb) end
    local bank_id = Catalog.bank_id(path)
    local b = self.banks[bank_id]
    if b and b.ready and self:bank_alive(bank_id) then
        if cb then cb(bank_id, b.motions) end
        return bank_id
    end
    local holder, err = E.resource_holder("via.motion.MotionListResource", path)
    if not holder then Log.err("load_motlist: %s", err); return nil end
    local db, slot = find_dynamic_bank(self.motion, bank_id)
    if not db then
        db = sdk.create_instance("via.motion.DynamicMotionBank"):add_ref()
        db:call(".ctor")
        db:call("set_MotionList", holder)
        db:call("set_OverwriteBankID", true)
        db:call("set_BankID", bank_id)
        self.motion:call("setDynamicMotionBankCount", slot + 1)
        self.motion:call("setDynamicMotionBank", slot, db)
    else
        db:call("set_MotionList", holder)
    end
    self.banks[bank_id] = { path = path, ready = false, motions = nil, frames = 0, cb = cb, db = db }
    Log.info("%s: loading %s as bank %d", self.name, path:match("[^/]+$"), bank_id)
    return bank_id
end

function Actor:bank_alive(bank_id)
    if not self.motion then return false end
    local ok, n = pcall(function() return self.motion:call("getMotionCount(System.UInt32)", bank_id) end)
    return ok and n and n > 0
end

function Actor:enumerate_motions(bank_id)
    local n = self.motion:call("getMotionCount(System.UInt32)", bank_id) or 0
    local out = {}
    for j = 0, n - 1 do
        local info = sdk.create_instance("via.motion.MotionInfo")
        info:call(".ctor")
        if self.motion:call("getMotionInfoByIndex(System.UInt32, System.UInt32, via.motion.MotionInfo)", bank_id, j, info) then
            out[#out + 1] = { id = info:call("get_MotionID"), name = info:call("get_MotionName"), endframe = info:call("get_MotionEndFrame") }
        end
    end
    return out
end

-- called every frame from Actor.tick
function Actor:poll_banks()
    for bank_id, b in pairs(self.banks) do
        if not b.ready then
            b.frames = b.frames + 1
            if self:bank_alive(bank_id) then
                b.ready = true
                local ok, mots = pcall(self.enumerate_motions, self, bank_id)
                b.motions = ok and mots or {}
                Catalog.remember_motions(b.path, b.motions)
                Log.info("%s: bank %d ready (%d motions, %d frames)", self.name, bank_id, #b.motions, b.frames)
                if b.cb then Log.try("bank cb", b.cb, bank_id, b.motions); b.cb = nil end
            elseif b.frames > 1200 then
                Log.err("%s: bank %d (%s) never loaded", self.name, bank_id, b.path)
                self.banks[bank_id] = nil
            end
        end
    end
end

function Actor:motions(bank_id)
    local b = self.banks[bank_id]
    return b and b.motions or Catalog.motions(Catalog.path_for_bank(bank_id) or "") or {}
end

-------------------------------------------------------------------------------
-- playback
-------------------------------------------------------------------------------
function Actor:layer_count()
    if not self.motion then return 0 end
    local ok, n = pcall(function() return self.motion:call("getLayerCount") end)
    return ok and n or 0
end

function Actor:layer(idx)
    if not self.motion then return nil end
    local ok, l = pcall(function() return self.motion:call("getLayer", idx) end)
    return ok and l or nil
end

function Actor:layer_info(idx)
    local l = self:layer(idx)
    if not l then return nil end
    local info = { idx = idx }
    pcall(function()
        info.bank = l:call("get_MotionBankID")
        info.mot = l:call("get_MotionID")
        info.frame = l:call("get_Frame")
        info.endframe = l:call("get_EndFrame")
        info.speed = l:call("get_Speed")
    end)
    return info
end

-- opts: layer (0), blend (frames, 10), start (frame, 0), speed (1), puppet (true)
-- the motion change lands on a later update and re-initialises the layer (a fresh TreeLayer comes back with speed 0);
-- get_Speed keeps reporting the value we set until then, so read-back is no signal: re-apply for ~half a second
local function ensure_speed(l, speed, tries)
    pcall(function() l:call("set_Speed", E.f(speed, 1)) end)
    if tries <= 0 then return end
    E.defer(function() ensure_speed(l, speed, tries - 1) end, 2)
end

function Actor:play(bank_id, mot_id, opts)
    opts = opts or {}
    local path = self.cast and Catalog.path_for_bank(bank_id)
    if wrong_rig(self, path) then Log.warn("%s: refusing animation from another character", self.name); return false end
    if self.cast and head_motlist(path) then
        local proxy = self.facial_banks and self.facial_banks[bank_id]
        if not (proxy and proxy.ready) then
            self:load_motlist(path, function() if self:valid() then self:play(bank_id, mot_id, opts) end end)
            return false
        end
        return require("Director.cast").play_facial(self, bank_id, mot_id, opts)
    end
    local layer_idx = opts.layer or 0
    if not self:bank_alive(bank_id) then
        local path = Catalog.path_for_bank(bank_id)
        if path then
            Log.warn("%s: bank %d not attached; loading %s then playing", self.name, bank_id, path:match("[^/]+$"))
            self:load_motlist(path, function() self:play(bank_id, mot_id, opts) end)
        else
            Log.err("%s: bank %d unknown", self.name, bank_id)
        end
        return false
    end
    if opts.puppet ~= false and self.fsm and layer_idx == 0 then self:set_puppet(true) end
    local l = self:layer(layer_idx)
    if not l then Log.err("%s: no layer %d", self.name, layer_idx); return false end
    l:call(CHANGE_MOTION, bank_id, mot_id, E.f(opts.start, 0), E.f(opts.blend, 10), 1, 0)
    if opts.speed then ensure_speed(l, opts.speed, 16) end
    self.last_play = { bank = bank_id, mot = mot_id, layer = layer_idx }
    if self.on_play then pcall(self.on_play, self) end
    return true
end

function Actor:set_speed(layer_idx, speed)
    local l = self:layer(layer_idx); if l then pcall(function() l:call("set_Speed", E.f(speed, 1)) end) end
end

-- pause/resume the whole Motion component (engine ignores layer speed 0; EMV uses PlayState 1 = paused)
function Actor:set_paused(on)
    if not self.motion then return end
    local ok, err = pcall(function() self.motion:call("set_PlayState", on and 1 or 0) end)
    if ok then self.paused = on and true or false else Log.err("%s: set_PlayState failed: %s", self.name, tostring(err)) end
end

function Actor:joint_count()
    if self.n_joints then return self.n_joints end
    local n = nil
    pcall(function()
        local arr = self.xform:call("get_Joints")
        if arr then n = #arr:get_elements() end
    end)
    if not n then pcall(function() n = self.xform:call("get_JointCount") end) end
    self.n_joints = n or 0
    return self.n_joints
end

-- make sure the player is registered; returns the Actor or nil
function Actor.ensure_player()
    local body = E.player_body()
    return body and Actor.wrap(body) or nil
end

function Actor:set_frame(layer_idx, frame)
    local l = self:layer(layer_idx); if l then pcall(function() l:call("set_Frame", E.f(frame, 0)) end) end
end

function Actor:set_puppet(on)
    if not self.fsm then return end
    if on then
        if self.puppet_orig == nil then self.puppet_orig = self.fsm:call("get_PuppetMode") end
        self.fsm:call("set_PuppetMode", true)
        self.puppet = true
    else
        pcall(function()
            self.fsm:call("set_PuppetMode", self.puppet_orig or false)
            self.fsm:call("restartTree")
        end)
        self.puppet = false
        self.puppet_orig = nil
    end
    if self.is_player then E.set_player_frozen(on) end
end

-------------------------------------------------------------------------------
-- root lock & joint overrides (applied after UpdateMotion)
-------------------------------------------------------------------------------
function Actor:set_root_lock(on)
    if on then
        local p = self.xform:call("get_Position")
        local r = self.xform:call("get_Rotation")
        self.root_lock = { pos = E.v3(p), rot = r }
    else
        self.root_lock = nil
    end
end

function Actor:set_root_pose(pos, rot)
    if self.root_lock then
        if pos then self.root_lock.pos = E.v3(pos) end
        if rot then self.root_lock.rot = rot end
    end
    if pos then self.xform:call("set_Position", E.v3(pos)) end
    if rot then self.xform:call("set_Rotation", rot) end
end

function Actor:joint(name)
    local j = self.override_joints[name]
    if j and E.valid(j) then return j end
    local ok, jj = pcall(function() return self.xform:call("getJointByName(System.String)", name) end)
    if ok and jj then self.override_joints[name] = jj end
    return ok and jj or nil
end

-- all joint names of this actor's transform (cached)
function Actor:joint_names()
    if self.jnames then return self.jnames end
    local names = {}
    pcall(function()
        local arr = self.xform:call("get_Joints")
        if arr then
            for _, j in ipairs(arr:get_elements()) do
                local ok, n = pcall(function() return j:call("get_Name") end)
                if ok and n then names[#names + 1] = n end
            end
        end
    end)
    self.jnames = names
    return names
end

function Actor:set_override(joint_name, quat)
    self.overrides[joint_name] = quat
end

function Actor:clear_overrides() self.overrides = {} end

function Actor:apply_after_motion()
    if self.root_lock then
        pcall(function()
            self.xform:call("set_Position", self.root_lock.pos)
            self.xform:call("set_Rotation", self.root_lock.rot)
        end)
    end
    for name, q in pairs(self.overrides) do
        local j = self:joint(name)
        if j then pcall(function() j:call("set_LocalRotation", q) end) end
    end
    if self.pose_layers and #self.pose_layers > 0 then Pose.apply(self) end
end

function Actor:has_pose_work()
    return self.root_lock ~= nil or next(self.overrides) ~= nil or (self.pose_layers and #self.pose_layers > 0)
end

function Actor:release()
    self.overrides = {}
    self.pose_layers = {}
    self.root_lock = nil
    self.detached = true -- the sequence leaves this actor alone until the transport is used again
    if self.paused then self:set_paused(false) end
    if self.puppet then self:set_puppet(false) end
end

-------------------------------------------------------------------------------
-- serialization
-------------------------------------------------------------------------------
function Actor:serialize()
    local banks = {}
    for bank_id, b in pairs(self.banks) do banks[#banks + 1] = { id = bank_id, path = b.path } end
    for bank_id, b in pairs(self.facial_banks or {}) do banks[#banks + 1] = { id = bank_id, path = b.path } end
    local layers = {}
    for i = 0, math.min(self:layer_count() - 1, 12) do
        local li = self:layer_info(i)
        if li and li.bank and li.bank >= 60000 then layers[#layers + 1] = { idx = i, bank = li.bank, mot = li.mot, speed = li.speed } end
    end
    local pos = self.xform:call("get_Position")
    local rot = self.xform:call("get_Rotation")
    local okS, scl = pcall(function() return self.xform:call("get_LocalScale") end)
    return {
        name = self.name, is_player = self.is_player, puppet = self.puppet,
        root_lock = self.root_lock ~= nil,
        pos = { pos.x, pos.y, pos.z }, rot = { rot.w, rot.x, rot.y, rot.z },
        scale = (okS and scl) and { scl.x, scl.y, scl.z } or nil,
        lookat = require("Director.lookat").serialize(self),
        cast = self.cast and require("Director.cast").describe(self) or nil, mesh = self.mesh_asset, display_name = self.display_name,
        banks = banks, layers = layers,
    }
end

-------------------------------------------------------------------------------
-- per-frame
-------------------------------------------------------------------------------
E.on_frame(function()
    for _, a in ipairs(Actor.all(true)) do a:poll_banks() end
end)

E.after_motion(function()
    for _, addr in ipairs(Actor.order) do
        local a = Actor.registry[addr]
        if a and a:has_pose_work() and a:valid() then a:apply_after_motion() end
    end
end)

return Actor
