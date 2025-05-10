local plot  = require("spaghetti.plot")
local check = require("spaghetti.check")
local misc  = require("spaghetti.misc")

local function parse_plan(ctx, plan)
	ctx.stacks = plan.stacks
	ctx.cost   = plan.part_count
	local calls = {}
	for i = 1, #plan.steps do
		local step = plan.steps[i]
		local info = assert(plot.particle_macros[step.type])
		local params = { info.func, ctx }
		for j = 1, #info.params do
			local key = info.params[j]
			local value = step[key] or false
			table.insert(params, value)
		end
		if step.type == "lfilt" then
			ctx.left13 = params[4] + 1
		end
		if step.type == "rfilt" and params[5] == plot.LSNS_LIFE_3 then
			ctx.right13 = params[4]
		end
		table.insert(calls, params)
	end
	return calls
end

local function partsify_plan(plan, extra)
	local optimize = _G.require("spaghetti.optimize")
	check.mt(optimize.plan_mt, "plan", plan)
	if extra ~= nil then
		check.table("extra", extra)
	end
	local ctx = {}
	local calls = parse_plan(ctx, plan)
	local parts = {}
	for i = 1, #calls do
		local func = calls[i][1]
		for _, part in ipairs(func(unpack(calls[i], 2))) do
			table.insert(parts, part)
		end
	end
	-- assert(#parts == ctx.cost) -- TODO
	if extra then
		for i = 1, #extra do
			table.insert(parts, extra[i])
		end
	end
	for _, part in ipairs(parts) do
		part.x = (part.x or 0)
		part.y = (part.y or 0)
	end
	return parts
end

local make_plan = misc.user_wrap(function(x, y, plan, extra, storage_remap, debug)
	local parts = partsify_plan(plan, extra)
	plot.create_parts_(x, y, parts, storage_remap, debug)
end)

local serialize_plan = misc.user_wrap(function(plan, extra, seed)
	local parts = partsify_plan(plan, extra)
	return plot.serialize_parts_(parts, seed)
end)

return {
	make_plan      = make_plan,
	serialize_plan = serialize_plan,
}
