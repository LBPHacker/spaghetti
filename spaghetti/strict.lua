local function wrap_env()
	local renv = getfenv(2)
	local env = setmetatable({}, {
		__index = function(_, key)
			if renv[key] == nil then
				error("global access with key " .. tostring(key), 2)
			end
			return renv[key]
		end,
		__newindex = function(_, key)
			error("global access with key " .. tostring(key), 2)
		end,
	})
	setfenv(2, env)
end

local function make_mt(name)
	local index = {}
	return { __index = function(tbl, key)
		if index[key] == nil then
			error("index on " .. name .. " with key " .. tostring(key), 2)
		end
		return index[key]
	end, __tostring = function()
		return name
	end, spaghetti_name_ = name }, index
end

local function make_mt_one(name, tbl)
	local mt = make_mt(name)
	return setmetatable(tbl, mt)
end

return make_mt_one("spaghetti.strict", {
	wrap_env    = wrap_env,
	make_mt     = make_mt,
	make_mt_one = make_mt_one,
})
