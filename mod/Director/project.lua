-- Director :: project save/load (reframework/data/director/projects/<name>.json)
local Log = require("Director.log")
local E = require("Director.engine")
local Actor = require("Director.actor")
local Cam = require("Director.camera")
local Seq = require("Director.sequence")

local P = { dir = "director/projects/", current = "scene1" }

local function file_for(name) return P.dir .. name .. ".json" end

function P.list()
    local out = {}
    local ok, files = pcall(fs.glob, "director\\\\projects\\\\.*\\.json")
    if ok and files then
        for _, f in ipairs(files) do
            local n = f:match("([^/\\]+)%.json$")
            if n then out[#out + 1] = n end
        end
    end
    table.sort(out)
    return out
end

function P.save(name)
    name = name or P.current
    local actors = {}
    for _, a in ipairs(Actor.all()) do actors[#actors + 1] = a:serialize() end
    local cams = Cam.serialize()
    -- record camera targets by actor name
    for i, c in ipairs(Cam.cams) do
        local a = c.target and Actor.get(c.target)
        cams.cams[i].target_name = a and a.name or nil
    end
    local Bridge = package.loaded["Director.bridge"]
    local data = { version = 4, saved = os.date("%Y-%m-%d %H:%M:%S"), actors = actors, cameras = cams, sequence = Seq.serialize(),
                   lights = require("Director.lights").serialize(), attachments = require("Director.extras").serialize_attachments(),
                   mix = Bridge and Bridge.mix or nil, constraints = require("Director.constraint").serialize() }
    local ok, err = pcall(json.dump_file, file_for(name), data, 2)
    if ok then Log.info("project saved: %s (%d actors, %d cameras)", name, #actors, #cams.cams)
    else Log.err("project save failed: %s", tostring(err)) end
    P.current = name
    return ok
end

-- resolve an actor by name among scene candidates (first unclaimed match)
local function resolve_actor(name, claimed)
    for _, c in ipairs(Actor.scan(nil)) do
        if c.name == name and not claimed[c.go:get_address()] then
            claimed[c.go:get_address()] = true
            return Actor.wrap(c.go)
        end
    end
    return nil
end

function P.load(name)
    local data = json.load_file(file_for(name))
    if not data then Log.err("project not found: %s", name); return false end
    local claimed, by_name = {}, {}
    local Cast = require("Director.cast")
    for _, ad in ipairs(data.actors or {}) do
        local a
        if ad.mesh and P.spawn_mesh then
            a = Actor.get_by_name(ad.name)
            if not (a and a.mesh_asset) then
                a = P.spawn_mesh({ mesh = ad.mesh.path, mdf = ad.mesh.mdf, name = ad.name,
                    pos = ad.pos, rot = ad.rot, scale = ad.scale })
            end
            if a then claimed[a.addr] = true end
        elseif ad.cast then -- spawned cast member: rebuild it (parts attach over the next frames), then treat it like any actor
            a = Actor.get_by_name and Actor.get_by_name(ad.name) or nil
            if not (a and a.cast) then
                local pos = ad.pos and Vector3f.new(ad.pos[1], ad.pos[2], ad.pos[3]) or nil
                local rot = ad.rot and E.quat(ad.rot[1], ad.rot[2], ad.rot[3], ad.rot[4]) or nil
                local err
                -- The numeric preset index is stable even when a host mangles
                -- accented display names such as Lucía or Tomás.
                a, err = Cast.spawn({ id = ad.cast.id, tree = ad.cast.tree, preset = ad.cast.preset or ad.cast.preset_name, name = ad.cast.name, pos = pos, rot = rot, no_idle = true })
                if not a then Log.warn("could not respawn %s: %s", tostring(ad.name), tostring(err)) end
            end
            if a then claimed[a.addr] = true end
        else
            a = resolve_actor(ad.name, claimed)
        end
        if not a then
            Log.warn("actor '%s' not found in this scene", ad.name)
        else
            by_name[ad.name] = a
            for _, b in ipairs(ad.banks or {}) do a:load_motlist(b.path) end
            if ad.pos and ad.rot then
                a:set_root_pose(Vector3f.new(ad.pos[1], ad.pos[2], ad.pos[3]), E.quat(ad.rot[1], ad.rot[2], ad.rot[3], ad.rot[4]))
            end
            if ad.scale then pcall(function() a.xform:call("set_LocalScale", Vector3f.new(ad.scale[1], ad.scale[2], ad.scale[3])) end) end
            if ad.puppet then a:set_puppet(true) end
            if ad.root_lock then a:set_root_lock(true) end
            if ad.lookat then a.pending_lookat = ad.lookat end
            -- replay layers once their banks are ready
            for _, l in ipairs(ad.layers or {}) do
                local path = l.bank and require("Director.catalog").path_for_bank(l.bank)
                if path then
                    a:load_motlist(path, function(bank_id) a:play(bank_id, l.mot, { layer = l.idx, blend = 0, speed = l.speed }) end)
                end
            end
        end
    end
    -- look-at targets need all actors resolved first
    local Lookat = require("Director.lookat")
    for _, a in pairs(by_name) do
        if a.pending_lookat then Lookat.deserialize(a, a.pending_lookat, function(n) return by_name[n] end); a.pending_lookat = nil end
    end
    if data.cameras then
        Cam.deserialize(data.cameras)
        for _, c in ipairs(Cam.cams) do
            local a = c.target_name and by_name[c.target_name]
            c.target = a and a.addr or nil
        end
    end
    if data.sequence then
        Seq.release()
        Seq.deserialize(data.sequence, function(n) return by_name[n] or resolve_actor(n, claimed) end)
    end
    pcall(function() require("Director.lights").deserialize(data.lights) end)
    pcall(function() require("Director.constraint").deserialize(data.constraints, function(n) return by_name[n] or Actor.get_by_name(n) end) end)
    if data.mix then
        local Bridge = package.loaded["Director.bridge"]
        if Bridge and Bridge.mix then
            for k, v in pairs(data.mix) do Bridge.mix[k] = v end
            pcall(Bridge.apply_mix)
        end
    end
    pcall(function() require("Director.extras").deserialize_attachments(data.attachments, function(n) return by_name[n] or Actor.get_by_name(n) end) end)
    P.current = name
    Log.info("project loaded: %s", name)
    return true
end

return P
