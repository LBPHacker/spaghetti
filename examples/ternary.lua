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

local unigate = require("spaghetti.unigate")

local cond = unigate.input(0x20000000, 0x0000FFFF)
cond:label("cond")
local vnz = unigate.input(0x20000000, 0x0000FFFF)
vnz:label("vnz")
local vz = unigate.input(0x20000000, 0x0000FFFF)
vz:label("vz")
local vnz2 = unigate.input(0x20000000, 0x0000FFFF)
vnz2:label("vnz2")
local vz2 = unigate.input(0x20000000, 0x0000FFFF)
vz2:label("vz2")
local res, res2 = ((cond % 0xFF) ^ 0x23):ternary(vnz, vz, vnz2, vz2)
res:label("res")
res2:label("res2")
res:assert(0x20000000, 0x0000FFFF)
res2:assert(0x20000000, 0x0000FFFF)

unigate.synthesize({
	[ 1 ] = cond,
	[ 3 ] = vnz,
	[ 5 ] = vz,
	[ 7 ] = vnz2,
	[ 9 ] = vz2,
}, {
	[ 2 ] = res,
	[ 6 ] = res2,
}, 10, 10, 1, 100, 100, rawget(_G, "tpt") and {
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -4, ctype = 0x20000001 },
	-- { type = elem.DEFAULT_PT_FILT, x = 6, y = -4, ctype = 0x20000000 },
	{ type = elem.DEFAULT_PT_LDTC, x = 6, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -4, ctype = 0x20005000 },
	{ type = elem.DEFAULT_PT_LDTC, x = 8, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 10, y = -4, ctype = 0x20000000 },
	{ type = elem.DEFAULT_PT_LDTC, x = 10, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 10, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 12, y = -4, ctype = 0x20105000 },
	{ type = elem.DEFAULT_PT_LDTC, x = 12, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 12, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 14, y = -4, ctype = 0x20100000 },
	{ type = elem.DEFAULT_PT_LDTC, x = 14, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 14, y = -1 },
	{ type = elem.DEFAULT_PT_LDTC, x = 7, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 7, y = 3 },
	{ type = elem.DEFAULT_PT_LDTC, x = 11, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 11, y = 3 },
} or nil)
