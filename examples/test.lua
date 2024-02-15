local strict = require("spaghetti.strict")
strict.wrap_env()

local spaghetti = require("spaghetti")

local lhs = spaghetti.input(0x10000000, 0x0001FFFF)
local rhs = spaghetti.input(0x10000000, 0x0000FFFF)
local bandv = spaghetti.band(lhs, rhs)
local borv = spaghetti.bor(lhs, rhs)
bandv:assert(0x10000000, 0x0000FFFF)
borv:assert(0x10000000, 0x0001FFFF)

return spaghetti.build({
	inputs = {
		[ 1 ] = lhs,
		[ 3 ] = rhs,
	},
	outputs = {
		[ 1 ] = bandv,
		[ 3 ] = borv,
	},
	stacks        = 1,
	storage_slots = 21,
	work_slots    = 8,
}), {
	{ type = elem.DEFAULT_PT_FILT, x = 3, y = -4, ctype = 0x1001DEAD },
	{ type = elem.DEFAULT_PT_LDTC, x = 3, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 3, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 5, y = -4, ctype = 0x1000BEEF },
	{ type = elem.DEFAULT_PT_LDTC, x = 5, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 5, y = -1 },
	{ type = elem.DEFAULT_PT_LDTC, x = 3, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 3, y = 3 },
	{ type = elem.DEFAULT_PT_LDTC, x = 5, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 5, y = 3 },
}
