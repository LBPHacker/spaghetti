local strict = require("spaghetti.strict")
strict.wrap_env()

local ordered_map_m, ordered_map_i = strict.make_mt("spaghetti.ordered_map.ordered_map")

function ordered_map_i:add(thing, value)
	if value == nil then
		value = true
	end
	if not self.items_[thing] then
		self.items_[thing] = value
		table.insert(self.order_, thing)
	end
end

function ordered_map_i:count()
	return #self.order_
end

function ordered_map_i:ipairs()
	local index = 1
	return function()
		local key = self.order_[index]
		if key then
			index = index + 1
			return key, self.items_[key]
		end
	end
end

function ordered_map_i:get(thing)
	return self.items_[thing]
end

local function make_ordered_map()
	return setmetatable({
		order_ = {},
		items_ = {},
	}, ordered_map_m)
end

function ordered_map_m:__newindex()
	error("fix me", 2)
end

return {
	make_ordered_map = make_ordered_map,
}
