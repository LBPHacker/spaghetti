assert(bit, "no bit API, are you running this under luajit?")

local strict = require("spaghetti.strict")
strict.wrap_env()

local check      = require("spaghetti.check")
local user_node  = require("spaghetti.user_node")
local build      = require("spaghetti.build")
local misc       = require("spaghetti.misc")

local select_group_m = strict.make_mt("spaghetti.select_group.select_group")

local function constant(keepalive, payload)
	return misc.user_wrap(function()
		return user_node.make_constant(keepalive, payload)
	end)
end

local function input(keepalive, payload)
	return misc.user_wrap(function()
		return user_node.make_input(keepalive, payload)
	end)
end

local function selectf(cond, ...) -- [ vnonzero[1], vzero[1], [ vnonzero[2], vzero[2], [ ... ] ] ]
	return misc.user_wrap(function(...)
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
	end, ...)
end

local spaghetti = strict.make_mt_one("spaghetti", {
	constant = constant,
	input    = input,
	build    = build.build,
})
for key, info in pairs(user_node.opnames_) do
	spaghetti[key] = function(...)
		return misc.user_wrap(function(lhs, ...)
			local params = { lhs, ... }
			for index, name in ipairs(info.params) do
				params[index] = user_node.maybe_promote_number_(params[index])
				check.mt(user_node.mt_, name, params[index])
			end
			return lhs[key .. "_"](unpack(params))
		end, ...)
	end
end
spaghetti.select = selectf -- the loop above would overwrite this otherwise

return spaghetti
