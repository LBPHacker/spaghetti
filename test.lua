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
local shift = spaghetti.shift
local band = spaghetti.band
local bxor = spaghetti.bxor
local bor = spaghetti.bor
local lshiftk = spaghetti.lshiftk
local constant = spaghetti.constant

local lhs = spaghetti.input(0x10000000, 0x0001FFFF) .. "lhs"
local carry_in = lhs:band(0x00010000)
local rhs = spaghetti.input(0x10000000, 0x0000FFFF) .. "rhs"
local lhs_ka = lhs:bor(0x00010000)
local rhs_ka = rhs:bor(0x3FFF0000)
local generate = band(lhs_ka, rhs_ka) .. { "generate_0", 0x10010000, 0x0000FFFF }
-- for i = 1, 4 do
-- 	local bit_i = bit.lshift(1, i)
-- 	local bit_i_m1 = bit.lshift(1, i - 1)
-- 	local generate_keepalive = bit.band(bit.bor(0x30000000, bit.lshift(bit.lshift(1, bit_i) - 1, 16)), 0x3FFFFFFF)
-- 	local propagate_fill = bit.bor(0x3FFF0000, bit.lshift(1, bit_i_m1) - 1)
-- 	generate = bor(generate, band(propagate, lshiftk(generate, bit_i_m1))) .. { "generate_" .. i, generate_keepalive, 0x0000FFFF }
-- 	propagate = band(propagate, bor(lshiftk(propagate, bit_i_m1), propagate_fill)) .. { "propagate_" .. i, 0x2FFE0000, 0x0000FFFF }
-- end
-- generate:assert(0x3FFF0000, 0x0000FFFF)
-- propagate:assert(0x2FFE0000, 0x0000FFFF)
-- local generate_shifted = lshiftk(generate, 1) .. { "generate_shifted", 0x3FFE0000, 0x0001FFFE }
-- local propagate_shifted = bor(lshiftk(propagate, 1), constant(0, 1)) .. { "propagate_shifted", 0x1FFC0000, 0x0001FFFF }
-- local propagate_conditional = carry_in:ternary(propagate_shifted, 0x1FFC0000) .. { "propagate_conditional", 0x1FFC0000, 0x0001FFFF }
-- local carries = bor(generate_shifted, propagate_conditional) .. { "carries", 0x3FFE0000, 0x0001FFFF }
-- local sum = bxor(onebit_sums, carries) .. { "sum", 0x10000000, 0x0001FFFF }

spaghetti.synthesize({
	[ 1 ] = lhs,
	[ 3 ] = rhs,
}, {
	[ 1 ] = generate,
}, 20, 30, 1, 100, 100, rawget(_G, "tpt") and {
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -4, ctype = 0x1000DEAD },
	{ type = elem.DEFAULT_PT_LDTC, x = 6, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = -1 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -4, ctype = 0x1000BEEF },
	{ type = elem.DEFAULT_PT_LDTC, x = 8, y = -2 },
	{ type = elem.DEFAULT_PT_FILT, x = 8, y = -1 },
	{ type = elem.DEFAULT_PT_LDTC, x = 6, y = 2 },
	{ type = elem.DEFAULT_PT_FILT, x = 6, y = 3 },
} or nil)
