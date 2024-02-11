local strict = require("spaghetti.strict")
strict.wrap_env()

local misc = require("spaghetti.misc")

local ctype_bits = 0x3FFFFFFF

local function typef(typev, name, value)
	if type(value) ~= typev then
		misc.user_error("%s is not of type %s", name, typev)
	end
end

local function number(name, value)
	typef("number", name, value)
end

local function func(name, value)
	typef("function", name, value)
end

local function tablef(name, value)
	typef("table", name, value)
end

local function integer(name, value)
	number(name, value)
	if math.floor(value) ~= value then
		misc.user_error("%s is not an integer", name)
	end
end

local function ctype(name, value)
	integer(name, value)
	if value < 0 or value > ctype_bits then
		misc.user_error("%s is not a ctype", name)
	end
end

local function keepalive(name, value)
	ctype(name, value)
end

local function payload(name, value)
	ctype(name, value)
end

local function mt(expected_mt, name, value)
	tablef(name, value)
	if getmetatable(value) ~= expected_mt then
		misc.user_error("%s is not of type %s", name, expected_mt.check_mt_name)
	end
end

return strict.make_mt_one("spaghetti.check", {
	typef      = typef,
	number     = number,
	func       = func,
	table      = tablef,
	integer    = integer,
	ctype      = ctype,
	keepalive  = keepalive,
	payload    = payload,
	mt         = mt,
	ctype_bits = ctype_bits,
})
