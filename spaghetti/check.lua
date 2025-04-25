local strict = require("spaghetti.strict")
local misc   = require("spaghetti.misc")

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

local function stringf(name, value)
	typef("string", name, value)
end

local function one_of(name, value, options)
	for i = 1, #options do
		if options[i] == value then
			return
		end
	end
	local options_strs = {}
	for i = 1, #options do
		table.insert(options_strs, tostring(options[i]))
	end
	misc.user_error("%s is not one of %s", name, table.concat(options_strs, ", "))
end

local function integer(name, value)
	number(name, value)
	if math.floor(value) ~= value then
		misc.user_error("%s is not an integer", name)
	end
end

local function integer_range(name, value, low, high)
	integer(name, value)
	if value < low or value > high then
		misc.user_error("%s is not in the range %i to %i inclusive", name, low, high)
	end
end

local function keepalive(name, value)
	integer(name, value)
	if value < 0 or value > keepalive_bits then
		misc.user_error("%s is not the valid keepalive range", name)
	end
end

local function kshift(name, value)
	integer(name, value)
	if value < 0 or value > 29 then
		misc.user_error("%s is not the valid constant shift", name)
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
	integer_range    = integer_range,
	string           = stringf,
	one_of           = one_of,
	shift_aware_bits = shift_aware_bits,
	keepalive_bits   = keepalive_bits,
	payload_bits     = payload_bits,
	keepalive        = keepalive,
	payload          = payload,
	kshift           = kshift,
	mt               = mt,
})
