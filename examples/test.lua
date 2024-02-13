#!/usr/bin/env luajit

local strict = require("spaghetti.strict")
strict.wrap_env()

local spaghetti = require("spaghetti.init") -- lua5.1 is broken and doesn't have ?/init.lua in package.path

local lhs = spaghetti.input(0x10000000, 0x0001FFFF)
local rhs = spaghetti.input(0x10000000, 0x0000FFFF)
local bandv = spaghetti.band(lhs, rhs)
local borv = spaghetti.bor(lhs, rhs)
bandv:assert(0x10000000, 0x0000FFFF)
borv:assert(0x10000000, 0x0001FFFF)

spaghetti.build({
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
})
