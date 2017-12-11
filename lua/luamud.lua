-- read a mud description json structure,
-- translate keywords to IP addresses
-- pass a packet to evaluate, return matches or not matches

cjson = require 'cjson'

-- main module
local luamud = {}

-- MUD description encapsulation
local mud = {}
mud.__index = mud

function luamud.mud_create_from_file(file)
    local f, err = io.open(file, "r")
    if f == nil then return nil, err end
    local f = assert(io.open(file, "rb"))
    local content = f:read("*all")
    f:close()
    return luamud.mud_create(content)
end

function luamud.mud_create(mud_json)
    local new_mud = {}
    setmetatable(new_mud, mud)
    mud.data, err = cjson.decode(mud_json)
    if mud.data == nil then return nil, err end
    mud, err = mud:validate()
    if mud == nil then return nil, err end
    -- if we need to fill in extra values (last-update?)
    return mud
end

-- Validates and returns self if valid
-- Returns nil, err if not
function mud:validate()
    print("[XX] validating MUD description")
    -- the main element should be "ietf-mud:mud"
    mud_data = self.data["ietf-mud:mud"]
    if mud_data == nil then
        return nil, "Top-level element not 'ietf-mud:mud'"
    end
    -- must contain the following elements:
    -- mud-url, systeminfo,
    if mud_data["mud-url"] == nil then
        return nil, "No element 'mud-url'"
    end
    if mud_data["last-update"] == nil then
        return nil, "No element 'last-update'"
    end
    -- cache-validity is optional, default returned by getter
    if mud_data["is-supported"] == nil then
        return nil, "No element 'is-supported'"
    end
    -- systeminfo is optional and has no default
    -- extensions is optional and has no default (should we set empty list?)

    --return "validation not implemented"
    return self
end

-- essentially, all direct functions are helper functions, since
-- we keep the internal data as a straight conversion from json
function mud:get_mud_url()
    return self.data["ietf-mud:mud"]["mud-url"]
end

-- warning: returns a string
function mud:get_last_update()
    return self.data["ietf-mud:mud"]["last-update"]
end

function mud:get_cache_validity()
    if self.data["ietf-mud:mud"]["cache-validity"] ~= nil then
        return self.data["ietf-mud:mud"]["cache-validity"]
    else
        return 48
    end
end

function mud:get_is_supported()
    return self.data["ietf-mud:mud"]["is-supported"]
end

function mud:to_json()
    return cjson.encode(self.data)
end

return luamud