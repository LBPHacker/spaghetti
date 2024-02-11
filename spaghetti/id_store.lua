local strict = require("spaghetti.strict")
strict.wrap_env()

local id_store_m, id_store_i = strict.make_mt("spaghetti.id_store.id_store")

function id_store_i:get(thing)
	if not self.ids_[thing] then
		self.last_ = self.last_ + 1
		self.ids_[thing] = self.last_
	end
	return self.ids_[thing]
end

local function make_id_store()
	return setmetatable({
		last_ = 0,
		ids_  = {},
	}, id_store_m)
end

return {
	make_id_store = make_id_store,
}
