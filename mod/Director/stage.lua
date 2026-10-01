-- Director :: stages — load any area of the game (village / castle / island) without a save file
--
-- RE4R streams the world in two layers: a *location* (loc40 … loc69, chainsaw.LocationSceneManager) and the
-- *environment stages* inside it (st45_200 …, chainsaw.EnvSceneManager). The map's area labels
-- (chainsaw.LevelMapDataManager._PointText) give us a name, a stage and a world position for every area.
-- Going somewhere = request the location + stage, wait until both are ready, then warp the player there
-- (chainsaw.CharacterManager.requestWarp — the door-transition path — with GroundAdsorber.warp as fallback).
-- Gravity is off while we wait so the player can never fall out of the world.
local Log = require("Director.log")
local E = require("Director.engine")

local Stage = { list_cache = nil, job = nil, status = nil }

local RUN, WAIT = 3, 0
-- Priorities: Preload=0, ScriptControl=1, SceneLoadData=2, Reservation=3. The game's own streaming requests at
-- SceneLoadData/Reservation and outranks ScriptControl, so a stage we asked for would silently drop back to Ready.
local PRIO_SCRIPT = 3
local MAPS = { [10000] = "Village", [20000] = "Castle", [30000] = "Island" }

local function nullable_status(v)
    local nt = sdk.find_type_definition("System.Nullable`1<chainsaw.EnvSceneStatus>")
    local nv = ValueType.new(nt)
    nv:set_field("_HasValue", true)
    nv:set_field("_Value", v)
    return nv
end

local function loc_of(stage) return math.floor(stage / 1000) * 1000 end
-- Prefectures (Pref40 village / Pref50 castle / Pref60 island) hold the lighting scenes, the space info and the
-- streaming catalog for a whole region. Without one, an area loads its geometry but is lit flat: no sky zones.
-- DANGER: asking PrefectureSceneManager for a second region while another one is running crashed the game
-- outright (2026-09-22, Pref40 requested while Pref50 was live). We never request one unless the caller opts in.
local function pref_of(stage) return math.floor(stage / 10000) * 10000 end

local function msg_text(guid)
    local msgt = sdk.find_type_definition("via.gui.message")
    if not msgt then return nil end
    local ok, s = pcall(function() return sdk.call_native_func(nil, msgt, "get", guid) end)
    if ok and s and s ~= "" then return tostring(s) end
    return nil
end

-- stage -> map scene (village / castle / island) from the catalog's map-to-section table
local function stage_maps()
    local out = {}
    pcall(function()
        local mgr = sdk.get_managed_singleton("chainsaw.LevelMapDataManager")
        local cat = mgr and mgr:call("get_Catalog")
        local m2s = cat and cat:get_field("MapToSection")
        local arr = m2s and m2s:get_field("Datas")
        for i = 0, (arr and arr:get_size() or 0) - 1 do
            local d = arr:get_element(i)
            local map = d and d:get_field("Map")
            local secs = d and d:get_field("Sections")
            for j = 0, (secs and secs:get_size() or 0) - 1 do
                local s = secs:get_element(j)
                local id = s and s:get_field("ID")
                if id and id > 0 then out[id] = map end
            end
        end
    end)
    return out
end

-- every named area: { stage, loc, name, map, pos = {x,y,z} }
function Stage.list(force)
    if Stage.list_cache and not force then return Stage.list_cache end
    local out = {}
    local ok, err = pcall(function()
        local mgr = sdk.get_managed_singleton("chainsaw.LevelMapDataManager")
        if not mgr then return end
        local d = mgr:get_field("_PointText")
        if not d then return end
        local maps = stage_maps()
        local entries = d:get_field("_entries")
        local n = d:call("get_Count")
        local seen = {}
        for i = 0, n - 1 do
            local e = entries:get_element(i)
            local v = e and e:get_field("value")
            local cnt = v and v:call("get_Count") or 0
            for j = 0, cnt - 1 do
                local pt = v:call("get_Item", j)
                local stage = pt:get_field("Stage")
                if stage and stage > 0 then
                    local pos = pt:get_field("Position")
                    local name = msg_text(pt:get_field("MessageID"))
                    if name and not seen[name .. stage] then
                        seen[name .. stage] = true
                        local map = maps[stage]
                        local mapname = MAPS[map] or (stage < 50000 and "Village" or stage < 59000 and "Castle" or "Island")
                        out[#out + 1] = { stage = stage, loc = loc_of(stage), name = name, map = mapname, pos = { pos.x, pos.y, pos.z } }
                    end
                end
            end
        end
    end)
    if not ok then Log.warn("stage list: %s", tostring(err)) end
    local order = { Village = 1, Castle = 2, Island = 3 }
    table.sort(out, function(a, b) if a.map ~= b.map then return (order[a.map] or 9) < (order[b.map] or 9) end return a.name < b.name end)
    -- never cache an empty list: right after a script reset the message table is not ready yet
    if #out > 0 then Stage.list_cache = out end
    return out
end

-- current location / stage of the player
function Stage.current()
    local out = {}
    pcall(function()
        local mm = sdk.get_managed_singleton("chainsaw.MapManager")
        local sec = mm and mm:call("get_CurrentPlayerMapSection")
        out.stage = sec and sec:get_field("ID") or nil
    end)
    return out
end

local function request_prefecture(pref, status)
    local m = sdk.get_managed_singleton("chainsaw.PrefectureSceneManager")
    if not m then return false end
    local ok = pcall(function() m:call("requestToChangeStatus", pref, nullable_status(status), PRIO_SCRIPT) end)
    return ok
end

-- is this region's lighting scene the one that is live right now?
function Stage.prefecture_live()
    local m = sdk.get_managed_singleton("chainsaw.PrefectureSceneManager")
    if not m then return nil end
    for _, pref in ipairs({ 40000, 50000, 60000, 80000, 90000 }) do
        local ld = m:call("getSceneInfo", pref)
        local rc = ld and ld:call("get_RefController")
        if rc and rc:call("get_TargetStatus") == RUN then return pref end
    end
    return nil
end

local function prefecture_loaded(pref) return Stage.prefecture_live() == pref end

local function request_location(loc, status)
    local m = sdk.get_managed_singleton("chainsaw.LocationSceneManager")
    if not m then return false end
    m:call("requestToChangeStatus", loc, nullable_status(status), PRIO_SCRIPT)
    return true
end

-- every stage of a location that the game knows about (45200 -> 45000..45999)
function Stage.stages_of(loc)
    local em = sdk.get_managed_singleton("chainsaw.EnvSceneManager")
    local out = {}
    if not em then return out end
    for sid = loc, loc + 999 do
        local ok, ex = pcall(function() return em:call("exists", sid) end)
        if ok and ex then out[#out + 1] = sid end
    end
    return out
end

-- keep the neighbourhood loaded: the free camera can leave the one stage we asked for
function Stage.load_around(stage, on)
    local em = sdk.get_managed_singleton("chainsaw.EnvSceneManager")
    if not em then return 0 end
    local loc = loc_of(stage)
    local n = 0
    for _, sid in ipairs(Stage.stages_of(loc)) do
        local okr = pcall(function() em:call("requestToChangeStatus", sid, nullable_status(on and RUN or WAIT), PRIO_SCRIPT) end)
        if okr then n = n + 1 end
    end
    Log.info("stage: %s %d stages of loc%d", on and "loading" or "releasing", n, loc / 1000)
    return n
end

local function request_env(stage, status)
    local m = sdk.get_managed_singleton("chainsaw.EnvSceneManager")
    if not m then return false end
    local ok, ex = pcall(function() return m:call("exists", stage) end)
    if not (ok and ex) then return false end
    m:call("requestToChangeStatus", stage, nullable_status(status), PRIO_SCRIPT)
    return true
end

-- Ready (2) or Run (3): the assets exist either way, which is all a camera needs
local function env_loaded(stage)
    local m = sdk.get_managed_singleton("chainsaw.EnvSceneManager")
    local ld = m and m:call("getSceneLoader", stage)
    return ld and ld:call("get_TargetStatus") >= 2 and ld:call("isRequestCompleted")
end

local function location_ready(loc)
    local m = sdk.get_managed_singleton("chainsaw.LocationSceneManager")
    local ld = m and m:call("getSceneInfo", loc)
    local rc = ld and ld:call("get_RefController")
    return rc and rc:call("get_TargetStatus") == RUN and rc:call("isRequestCompleted")
end

local function env_ready(stage)
    local m = sdk.get_managed_singleton("chainsaw.EnvSceneManager")
    local ld = m and m:call("getSceneLoader", stage)
    return ld and ld:call("get_TargetStatus") == RUN and ld:call("isRequestCompleted")
end

-- physics ray with the player's own collision filter (layer 2 / mask 796): the walkable ground
function Stage.ground(x, y, z, up, down)
    local best
    pcall(function()
        local sys = sdk.get_native_singleton("via.physics.System")
        local st = sdk.find_type_definition("via.physics.System")
        local q = sdk.create_instance("via.physics.CastRayQuery"):add_ref()
        local r = sdk.create_instance("via.physics.CastRayResult"):add_ref()
        q:call("setRay", Vector3f.new(x, y + (up or 30), z), Vector3f.new(x, y - (down or 30), z))
        q:call("enableAllHits")
        local fi = q:call("get_FilterInfo")
        fi:call("set_Layer", 2); fi:call("set_MaskBits", 796)
        sdk.call_native_func(sys, st, "castRay(via.physics.CastRayQuery, via.physics.CastRayResult)", q, r)
        for i = 0, math.min(r:call("get_NumContactPoints") - 1, 15) do
            local cp = r:call("getContactPoint", i)
            local p = cp and cp:get_field("Position")
            -- nearest floor to the requested height wins (labels sit close to the floor)
            if p and (not best or math.abs(p.y - y) < math.abs(best - y)) then best = p.y end
        end
    end)
    return best
end

-- walkable ground near a map label: the label may sit inside an unloaded interior or above a roof, so look in
-- a widening ring around it (up to ~10 m) and take the hit closest to the label; returns x, y, z
function Stage.find_ground(x, y, z, radius, column_only)
    -- the label's own column first: its floor is the area's floor, whatever height the label itself sits at
    local straight = Stage.ground(x, y, z, 80, 80)
    if straight then return x, straight, z end
    if column_only then return nil end
    local best, bd
    local rings = { 1.5, 3, 4.5, 6, 8, 10 }
    for _, r in ipairs(rings) do
        if r <= (radius or 10) then
            local n = 8
            for k = 0, n - 1 do
                local a = k / n * 2 * math.pi
                local px, pz = x + math.cos(a) * r, z + math.sin(a) * r
                local gy = Stage.ground(px, y, pz, 60, 60)
                if gy then
                    local d = r + math.abs(gy - y) * 0.25
                    if not bd or d < bd then best, bd = { px, gy, pz }, d end
                end
            end
            if best and r >= 3 then break end
        end
    end
    if best then return best[1], best[2], best[3] end
    return nil
end

local function player_ctx_id()
    local cm = sdk.get_managed_singleton("chainsaw.CharacterManager")
    local ctx = cm and cm:call("getPlayerContextRef")
    if not ctx then return nil end
    local ok, id = pcall(function() return ctx:call("get_ContextID") end)
    if ok and id then return id end
    ok, id = pcall(function() return ctx:get_field("<ContextID>k__BackingField") end)
    return ok and id or nil
end

local function set_gravity(on)
    local pb = E.player_body()
    local ga = pb and E.component(pb, "chainsaw.GroundAdsorber")
    if ga then pcall(function() ga:call("set_EnabledGravity", on and true or false); if on then ga:call("resetVelocity") end end) end
end

local function warp_player(pos, rot, stage)
    local pb = E.player_body()
    if not pb then return false end
    local ok = false
    -- 1. the game's own warp (door transitions): position + rotation + stage identifier
    pcall(function()
        local cm = sdk.get_managed_singleton("chainsaw.CharacterManager")
        local cid = player_ctx_id()
        if not (cm and cid) then return end
        local nvt = sdk.find_type_definition("System.Nullable`1<via.vec3>")
        local nqt = sdk.find_type_definition("System.Nullable`1<via.Quaternion>")
        local np = ValueType.new(nvt); np:set_field("_HasValue", true); np:set_field("_Value", pos)
        local nq = ValueType.new(nqt); nq:set_field("_HasValue", true); nq:set_field("_Value", rot)
        local si = ValueType.new(sdk.find_type_definition("chainsaw.StageIdentifier"))
        si:call(".ctor(chainsaw.StageID, via.vec3)", stage, pos)
        cm:call("requestWarp", cid, np, nq, si)
        ok = true
    end)
    -- 2. the body's ground adsorber (always, so the position is right this frame)
    pcall(function()
        local ga = E.component(pb, "chainsaw.GroundAdsorber")
        if ga then ga:call("warp", pos, rot, true); ok = true end
    end)
    return ok
end

-- The sun, sky and fog belong to the location; the game swaps them when the player crosses a section border,
-- which never happens when we warp. Do it by hand: register the scene's directional light controller with the
-- light manager for this location, then ask the sky to re-pick its parameters.
function Stage.apply_lighting(loc)
    local done = {}
    pcall(function()
        local scene = sdk.call_native_func(sdk.get_native_singleton("via.SceneManager"), sdk.find_type_definition("via.SceneManager"), "get_CurrentScene")
        local lm = sdk.get_managed_singleton("chainsaw.LightManager")
        if not (scene and lm) then return end
        local arr = scene:call("findComponents(System.Type)", sdk.typeof("chainsaw.DirectionalLightController"))
        for i = 0, (arr and arr:get_size() or 0) - 1 do
            local c = arr:get_element(i)
            if c then pcall(function() lm:call("regist", c) end) end
        end
        lm:call("switchDirectionalLight", loc)
        done.sun = true
    end)
    pcall(function()
        local sm = sdk.get_managed_singleton("chainsaw.SkyManager")
        if not sm then return end
        sm:call("requestRefresh")
        done.sky = true
    end)
    return done
end

-- go to an area: { stage=, pos={x,y,z}, name= }
function Stage.go(entry)
    if not entry or not entry.stage then return false, "no stage" end
    if not (entry.pos and entry.pos[1] and entry.pos[2] and entry.pos[3]) then return false, "no area position" end
    if Stage.job then return false, "an area is already loading" end
    local pb = E.player_body()
    if not pb then return false, "no player in the world (load a save first)" end
    local tf = pb:call("get_Transform")
    local p0 = tf:call("get_Position")
    local loc = loc_of(entry.stage)
    local pref = pref_of(entry.stage)
    -- only when the region's lighting scene is already the live one (same region jump), or the caller insists
    if entry.load_prefecture or prefecture_loaded(pref) then request_prefecture(pref, RUN) end
    if not request_location(loc, RUN) then return false, "location loader unavailable" end
    -- The environment loader may not know the stage until its location is ready.
    request_env(entry.stage, RUN)
    -- Keep the player on their current floor while assets stream. Gravity is
    -- disabled only for the short, verified move after the target floor exists.
    Stage.job = { entry = entry, loc = loc, pref = pref, t0 = os.clock(), phase = "loading", origin = { pos = Vector3f.new(p0.x, p0.y, p0.z), rot = tf:call("get_Rotation") }, warped = false, frames = 0 }
    local live = Stage.prefecture_live()
    Stage.status = { name = entry.name, phase = "loading", started = os.clock(),
                     other_region = (live and live ~= pref) and true or nil }
    Log.info("stage: going to %s (st%d, loc%d)", tostring(entry.name), entry.stage, loc / 1000)
    return true
end

-- per frame
function Stage.tick()
    local job = Stage.job
    if not job then return end
    local now = os.clock()
    local pb = E.player_body()
    if not pb then
        set_gravity(true)
        Stage.job = nil; Stage.status = { name = job.entry.name, phase = "failed", error = "player vanished" }; return
    end
    local e = job.entry
    if job.phase == "loading" then
        job.frames = job.frames + 1
        if job.frames % 60 == 1 then request_env(e.stage, RUN) end
        local ready = location_ready(job.loc) and env_ready(e.stage)
        if not ready and now - job.t0 > 8 then ready = env_loaded(e.stage) end -- the game may hold a stage at Ready
        local gx, gy, gz
        if ready then gx, gy, gz = Stage.find_ground(e.pos[1], e.pos[2], e.pos[3], 10, now - job.t0 < 12) end
        if gy then
            job.phase = "warping"; job.t1 = now
            Stage.status.phase = "warping"
            local rot = job.origin.rot
            job.target = Vector3f.new(gx, gy + 0.15, gz)
            set_gravity(false)
            warp_player(job.target, rot, e.stage)
            job.warped = true
        elseif now - job.t0 > 30 then
            Stage.job = nil
            Stage.status = { name = e.name, phase = "failed", error = ready and "no walkable floor loaded" or "area did not finish loading" }
            Log.warn("stage: %s did not load; player stayed at origin", tostring(e.name))
        end
    elseif job.phase == "warping" then
        -- wait for solid ground under the player; the stream around him keeps loading meanwhile
        local p = pb:call("get_Transform"):call("get_Position")
        local gx, gy, gz = Stage.find_ground(e.pos[1], e.pos[2], e.pos[3])
        if gy then
            if math.abs(gy - p.y) > 0.4 or math.abs(gx - p.x) + math.abs(gz - p.z) > 0.5 then warp_player(Vector3f.new(gx, gy + 0.1, gz), job.origin.rot, e.stage) end
            set_gravity(true)
            Stage.job = nil
            local live = Stage.prefecture_live()
            Stage.status = { name = e.name, phase = "done", stage = e.stage,
                             flat_light = (live and live ~= job.pref) and true or nil }
            Log.info("stage: arrived at %s (ground %.2f)", tostring(e.name), gy)
            Log.try("stage neighbours", Stage.load_around, e.stage, true)
            Log.try("stage lighting", Stage.apply_lighting, job.loc)
            if Stage.on_arrive then pcall(Stage.on_arrive, e) end
        elseif now - job.t1 > 1.5 and not job.rewarped then
            job.rewarped = true
            warp_player(job.target, job.origin.rot, e.stage) -- the first warp can be swallowed while the scene settles
        elseif now - job.t1 > 12 then
            -- nothing solid appeared: put the player back where they were, never leave them falling
            warp_player(job.origin.pos, job.origin.rot, 0)
            set_gravity(true)
            Stage.job = nil
            Stage.status = { name = e.name, phase = "failed", error = "the area did not load (no ground)" }
            Log.warn("stage: %s did not load, player restored", tostring(e.name))
        end
    end
end

function Stage.cancel()
    if Stage.job then
        if Stage.job.warped then warp_player(Stage.job.origin.pos, Stage.job.origin.rot, 0) end
        set_gravity(true)
        Stage.job = nil
        Stage.status = nil
    end
end

function Stage.state()
    return { busy = Stage.job ~= nil, status = Stage.status, current = Stage.current() }
end

return Stage
