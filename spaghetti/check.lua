local strict = require("spaghetti.strict")
strict.wrap_env()

local misc = require("spaghetti.misc")

local shift_aware_bits = 0x3FFFFFFF
local keepalive_bits = 0x3FFFFFFF
local payload_bits = 0xFFFFFFFF

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

local function keepalive(name, value)
	integer(name, value)
	if value < 0 or value > keepalive_bits then
		misc.user_error("%s is not the valid keepalive range", name)
	end
end

local function payload(name, value)
	integer(name, value)
	if value < 0 or value > payload_bits then
		misc.user_error("%s is not the valid payload range", name)
	end
end

local function mt(expected_mt, name, value)
	tablef(name, value)
	if getmetatable(value) ~= expected_mt then
		misc.user_error("%s is not of type %s", name, expected_mt.check_mt_name)
	end
end

return strict.make_mt_one("spaghetti.check", {
	typef            = typef,
	number           = number,
	func             = func,
	table            = tablef,
	integer          = integer,
	shift_aware_bits = shift_aware_bits,
	keepalive_bits   = keepalive_bits,
	payload_bits     = payload_bits,
	keepalive        = keepalive,
	payload          = payload,
	mt               = mt,
})
