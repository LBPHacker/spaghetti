#!/usr/bin/env luajit

local env = getfenv(1)
setfenv(1, setmetatable({}, {
	__index = function(tbl, key)
		if env[key] then
			return env[key]
		end
		error("invalid __index", 2)
	end,
	__newindex = function(tbl, key, value)
		error("invalid __newindex", 2)
	end,
}))

local spaghetti = require("spaghetti")

local lhs = spaghetti.input(0x20000000, 0x0000FFFF)
lhs:label("lhs")
local rhs = spaghetti.input(0x20000000, 0x0000FFFF)
rhs:label("rhs")
local res = (lhs % rhs) % 0x200000FF
res:label("res")
res:assert(0x20000000, 0x000000FF)

spaghetti.synthesize({
	[ 1 ] = lhs,
	[ 3 ] = rhs,
}, {
	[ 1 ] = res,
}, 10, 10, 1, 100, 100, rawget(_G, "tpt") and {
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -4, ctype = 0x2000DEAD },
	{ type = elem.DEFAULT_PT_LDTC, x = 6, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -4, ctype = 0x2000BEEF },
	{ type = elem.DEFAULT_PT_LDTC, x = 8, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -1 },
	{ type = elem.DEFAULT_PT_LDTC, x = 6, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = 3 },
} or nil)
