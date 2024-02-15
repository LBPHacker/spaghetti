#!/usr/bin/env luajit

local strict = require("spaghetti.strict")
strict.wrap_env()

local spaghetti = require("spaghetti.init") -- lua5.1 is broken and doesn't have ?/init.lua in package.path
local optimize  = require("spaghetti.optimize")

local band     = spaghetti.band
local bxor     = spaghetti.bxor
local bor      = spaghetti.bor
local constant = spaghetti.constant

local function lshiftk(node, k)
	return node:lshift(bit.lshift(1, k))
end

local function ks(lhs, rhs)
	lhs:assert(0x10000000, 0x0001FFFF)
	local carry_in = lhs:band(0x00010000):zeroable()
	rhs:assert(0x10000000, 0x0000FFFF)
	local lhs_ka = lhs:bor(0x00010000)
	local rhs_ka = rhs:bor(0x3FFF0000)
	local generate = band(lhs_ka, rhs_ka) .. { 0x10010000, 0x0000FFFF }
	local propagate = bxor(lhs_ka, rhs_ka) .. { 0x2FFE0000, 0x0000FFFF }
	local onebit_sums = propagate
	for i = 1, 4 do
		local bit_i = bit.lshift(1, i)
		local bit_i_m1 = bit.lshift(1, i - 1)
		local generate_keepalive = bit.band(bit.bor(0x30000000, bit.lshift(bit.lshift(1, bit_i) - 1, 16)), 0x3FFFFFFF)
		local propagate_fill = constant(0x3FFF0000, bit.lshift(1, bit_i_m1) - 1)
		generate = bor(generate, band(propagate, lshiftk(generate, bit_i_m1))) .. { generate_keepalive, 0x0000FFFF }
		propagate = band(propagate, bor(lshiftk(propagate, bit_i_m1), propagate_fill)) .. { 0x2FFE0000, 0x0000FFFF }
	end
	generate:assert(0x3FFF0000, 0x0000FFFF)
	propagate:assert(0x2FFE0000, 0x0000FFFF)
	local generate_shifted = lshiftk(generate, 1) .. { 0x3FFE0000, 0x0001FFFE }
	local propagate_shifted = bor(lshiftk(propagate, 1), constant(0, 1)) .. { 0x1FFC0000, 0x0001FFFF }
	local propagate_conditional = carry_in:select(propagate_shifted, 0x1FFC0000) .. { 0x1FFC0000, 0x0001FFFF }
	local carries = bor(generate_shifted, propagate_conditional) .. { 0x3FFE0000, 0x0001FFFF }
	local sum = bxor(onebit_sums, carries) .. { 0x10000000, 0x0001FFFF }
	return sum
end

local lhs = spaghetti.input(0x10000000, 0x0001FFFF)
local rhs = spaghetti.input(0x10000000, 0x0000FFFF)
local sum = ks(lhs, rhs)
sum:assert(0x10000000, 0x0001FFFF)

return spaghetti.build({
	inputs = {
		[ 1 ] = lhs,
		[ 3 ] = rhs,
	},
	outputs = {
		[ 1 ] = sum,
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
}
