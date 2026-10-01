-- Director :: motlist catalog (offline JSON) + runtime motion-name cache
local Log = require("Director.log")

local C = { loaded = false, list = {}, by_path = {}, chars = {}, groups = {}, owners = {} }
local CATALOG_PATH = "director/catalog/motlists.json"
local BANK_BASE = 60000            -- bank id = BANK_BASE + catalog index (stable across sessions)
local CACHE_DIR = "director/cache/motions/"

function C.load()
    if C.loaded then return true end
    local data = json.load_file(CATALOG_PATH)
    if not data or not data.motlists then
        Log.err("catalog missing: reframework/data/%s (run tools/scripts/build_catalog.py)", CATALOG_PATH)
        return false
    end
    C.list = data.motlists
    C.chars = data.chars or {}
    local groups, owners = {}, {}
    for _, e in ipairs(C.list) do
        C.by_path[e.p:lower()] = e
        groups[e.g] = true
        -- Character animation owners use chxx. Cutscenes (csa/cse), weapons and
        -- props stay available through All, but do not clutter the character picker.
        if e.c and e.c:match("^ch[%w]+$") then owners[e.c] = (owners[e.c] or 0) + 1 end
    end
    C.groups = {}
    for g in pairs(groups) do C.groups[#C.groups + 1] = g end
    table.sort(C.groups)
    C.owners = {}
    for code, count in pairs(owners) do C.owners[#C.owners + 1] = { code = code, count = count } end
    table.sort(C.owners, function(a, b)
        local an, bn = C.chars[a.code] or a.code, C.chars[b.code] or b.code
        if an == bn then return a.code < b.code end
        return an < bn
    end)
    C.loaded = true
    Log.info("catalog loaded: %d motlists, %d groups", #C.list, #C.groups)
    return true
end

function C.entry(path)
    C.load()
    return C.by_path[path:lower()]
end

-- stable bank id for a motlist path (unknown paths get ids above the catalog range)
local adhoc = {}
local adhoc_next = BANK_BASE + 20000
function C.bank_id(path)
    local e = C.entry(path)
    if e then return BANK_BASE + e.i end
    local k = path:lower()
    if not adhoc[k] then adhoc[k] = adhoc_next; adhoc_next = adhoc_next + 1 end
    return adhoc[k]
end

function C.path_for_bank(bank_id)
    C.load()
    local i = bank_id - BANK_BASE
    local e = C.list[i + 1]
    if e and e.i == i then return e.p end
    for k, v in pairs(adhoc) do if v == bank_id then return k end end
    return nil
end

-- search: text (matched against name/path/char), group and character owner filters.
-- Filters run before the result cap so a non-Leon character can never be crowded out.
function C.search(text, group, owner, limit)
    C.load()
    limit = limit or 200
    local out = {}
    local needles = {}
    for w in (text or ""):lower():gmatch("%S+") do needles[#needles + 1] = w end
    for _, e in ipairs(C.list) do
        if (not group or e.g == group) and (not owner or e.c == owner) then
            local hay = (e.p .. " " .. e.c .. " " .. (C.chars[e.c] or "")):lower()
            local ok = true
            for _, w in ipairs(needles) do
                if not hay:find(w, 1, true) then ok = false; break end
            end
            if ok then
                out[#out + 1] = e
                if #out >= limit then break end
            end
        end
    end
    return out
end

function C.display_name(e)
    local who = C.chars[e.c] or e.c
    if who ~= "" then return string.format("%s  [%s]", e.n, who) end
    return e.n
end

-------------------------------------------------------------------------------
-- motion-name cache: motions discovered at runtime per motlist
-------------------------------------------------------------------------------
local mem = {}
local function cache_file(path)
    local e = C.entry(path)
    local key = e and tostring(e.i) or (path:lower():gsub("[^%w]", "_"))
    return CACHE_DIR .. key .. ".json"
end

-- list = { {id=, name=, endframe=}, ... }
function C.remember_motions(path, list)
    mem[path:lower()] = list
    pcall(json.dump_file, cache_file(path), { path = path, motions = list }, 0)
end

function C.motions(path)
    if not path or path == "" then return nil end
    local k = path:lower()
    local cached = mem[k]
    if cached ~= nil then return cached or nil end          -- false = known miss (avoids per-frame disk reads)
    local data = json.load_file(cache_file(path))
    if data and data.motions then mem[k] = data.motions; return data.motions end
    mem[k] = false
    return nil
end

function C.motion_name(path, mot_id)
    local list = C.motions(path)
    if not list then return nil end
    for _, m in ipairs(list) do if m.id == mot_id then return m.name end end
    return nil
end

return C
