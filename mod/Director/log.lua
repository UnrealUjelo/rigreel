-- Director :: logging (ring buffer + file under reframework/data/director/)
local M = { lines = {}, max = 400, path = "director/director_log.txt", dirty = false, last_flush = 0 }

local function push(level, fmt, ...)
    local ok, msg = pcall(string.format, fmt, ...)
    if not ok then msg = tostring(fmt) end
    msg = os.date("%H:%M:%S ") .. level .. " " .. msg
    M.lines[#M.lines + 1] = msg
    if #M.lines > M.max then table.remove(M.lines, 1) end
    log.info("[Director] " .. msg)
    M.dirty = true
    return msg
end

function M.info(fmt, ...) return push("I", fmt, ...) end
function M.warn(fmt, ...) return push("W", fmt, ...) end
function M.err(fmt, ...)  return push("E", fmt, ...) end

-- call once per frame; writes at most every ~0.5s
function M.tick()
    if not M.dirty then return end
    local now = os.clock()
    if now - M.last_flush < 0.5 then return end
    M.last_flush = now
    M.dirty = false
    pcall(fs.write, M.path, table.concat(M.lines, "\n"))
end

-- run fn in protected mode; log failures with a label
function M.try(label, fn, ...)
    local ok, err = pcall(fn, ...)
    if not ok then M.err("%s failed: %s", label, tostring(err)) end
    return ok, err
end

function M.tail(n)
    local out, start = {}, math.max(1, #M.lines - (n or 15) + 1)
    for i = start, #M.lines do out[#out + 1] = M.lines[i] end
    return out
end

return M
