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
	make_mt     = make_mt,
	make_mt_one = make_mt_one,
})
