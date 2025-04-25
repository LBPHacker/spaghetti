local strict     = require("spaghetti.strict")
local check      = require("spaghetti.check")
local user_node  = require("spaghetti.user_node")
local build      = require("spaghetti.build")
local misc       = require("spaghetti.misc")
local bitx       = require("spaghetti.bitx")

local audited_pairs = pairs

local select_group_m = strict.make_mt("spaghetti.select_group.select_group")

local constant = misc.user_wrap(function(keepalive, payload)
	return user_node.make_constant_(keepalive, payload)
end)

local input = misc.user_wrap(function(keepalive, payload)
	return user_node.make_input_(keepalive, payload)
end)

local selectf = misc.user_wrap(function(cond, ...) -- [ vnonzero[1], vzero[1], [ vnonzero[2], vzero[2], [ ... ] ] ]
	check.mt(user_node.mt_, "cond", cond)
	local select_group = setmetatable({}, select_group_m)
	for i = 1, math.ceil(select("#", ...) / 2) do
		local vnonzero, vzero = select(i * 2 - 1, ...)
		vnonzero = user_node.maybe_promote_number_(vnonzero)
		vzero = user_node.maybe_promote_number_(vzero)
		check.mt(user_node.mt_,  "vnonzero[" .. i .. "]", vnonzero)
		check.mt(user_node.mt_,     "vzero[" .. i .. "]",    vzero)
		local selected = cond:select_(vnonzero, vzero)
		selected.select_group_ = select_group
		select_group[i] = selected
	end
	return unpack(select_group) -- [ selected[1], [ selected[2], [ ... ] ] ]
end)

local lshiftk = misc.user_wrap(function(expr, amount)
	expr = user_node.maybe_promote_number_(expr)
	check.mt(user_node.mt_, "expr", expr)
	check.kshift("amount", amount)
	return expr:lshift(user_node.make_constant_(bitx.lshift(1, amount)))
end)

local rshiftk = misc.user_wrap(function(expr, amount)
	expr = user_node.maybe_promote_number_(expr)
	check.mt(user_node.mt_, "expr", expr)
	check.kshift("amount", amount)
	return expr:rshift(user_node.make_constant_(bitx.lshift(1, amount)))
end)

local spaghetti = strict.make_mt_one("spaghetti", {
	constant = constant,
	input    = input,
	lshiftk  = lshiftk,
	rshiftk  = rshiftk,
	build    = build.build,
})
for key, info in audited_pairs(user_node.opnames_) do
	spaghetti[key] = misc.user_wrap(function(lhs, ...)
		lhs = user_node.maybe_promote_number_(lhs)
		local params = { lhs, ... }
		for index, name in ipairs(info.params) do
			params[index] = user_node.maybe_promote_number_(params[index])
			check.mt(user_node.mt_, name, params[index])
		end
		return lhs[key .. "_"](unpack(params))
	end)
end
spaghetti.select = selectf -- the loop above would overwrite this otherwise

return spaghetti
