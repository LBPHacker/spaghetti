local strict = require("spaghetti.strict")
strict.wrap_env()

local build    = require("spaghetti.build")
local optimize = require("spaghetti.optimize")

local pt = setmetatable({}, { __index = function(tbl, key)
	return elem["DEFAULT_PT_" .. key]
end })

local particle_macros = {
	[ "lcap" ] = function(ctx, stack_index, life3_index)
		return {
			{ type = pt.FILT, x = -ctx.left13 * 2 - 4, ctype = build.LSNS_LIFE_3 },
			{ type = pt.DMND, x = -ctx.left13 * 2 - 3 },
		}
	end,
	[ "lfilt" ] = function(ctx, stack_index, work_slot)
		return {
			{ type = pt.FILT, x = -work_slot * 2 - 3 },
		}
	end,
	[ "rfilt" ] = function(ctx, stack_index, storage_slot, constant_value)
		return {
			{ type = pt.FILT, x = storage_slot + ctx.stacks * 3, ctype = constant_value },
		}
	end,
	[ "bottom" ] = function(ctx, stack_index)
		return {
			{ type = pt.FILT, x = stack_index * 3 - 1 },
			{ type = pt.CONV, x = stack_index * 3 + 1, tmp = pt.SPRK, ctype = pt.PSCN },
			{ type = pt.CONV, x = stack_index * 3 + 1, tmp = pt.PSCN, ctype = pt.SPRK },
			{ type = pt.SPRK, x = stack_index * 3 + 1, ctype = pt.PSCN, life = 4 },
		}
	end,
	[ "top" ] = function(ctx, stack_index)
		return {
			{ type = pt.STOR, x = stack_index * 3 },
		}
	end,
	[ "mode" ] = function(ctx, stack_index, tmp)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in co p3
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.INSL, ctype = bit.bor(pt.FILT, bit.lshift(tmp, sim.PMAPBITS)) },
			--          fi    p3
		}
	end,
	[ "load" ] = function(ctx, stack_index, work_slot, storage_slot)
		return {
			--          fi ld p3
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = storage_slot - 1 + (ctx.stacks - stack_index) * 3 },
			--          fi dr p3
			{ type = pt.DRAY, x = stack_index * 3, tmp = 1, tmp2 = work_slot * 2 + 1 + stack_index * 3 },
			--          fi    p3
		}
	end,
	[ "cload" ] = function(ctx, stack_index, work_slot)
		return {
			--          fi dr p3
			{ type = pt.DRAY, x = stack_index * 3, tmp = 1, tmp2 = work_slot * 2 + 1 + stack_index * 3 },
			--          fi    p3
		}
	end,
	[ "cstore" ] = function(ctx, stack_index, work_slot, storage_slot)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = work_slot * 2 + 3 + stack_index * 3 },
			--          p3    fi
		}
	end,
	[ "store" ] = function(ctx, stack_index, work_slot, storage_slot)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = work_slot * 2 + 3 + stack_index * 3 },
			--          p3 dr fi
			{ type = pt.DRAY, x = stack_index * 3, tmp = 1, tmp2 = storage_slot - 2 + (ctx.stacks - stack_index) * 3 },
			--          p3    fi
		}
	end,
	[ "aray" ] = function(ctx, stack_index)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.SPRK, ctype = pt.INST },
			--          fi co it
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.INST, ctype = pt.SPRK },
			--          fi ld i4
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = ctx.right13 - 1 + (ctx.stacks - stack_index) * 3 },
			--          fi ls i4
			{ type = pt.LSNS, x = stack_index * 3, tmp = 3, tmp2 = 1 },
			--          fi ar i3
			{ type = pt.ARAY, x = stack_index * 3 },
			--          fi    i3
		}
	end,
	[ "clear" ] = function(ctx, stack_index)
		return {
			--          fi cr p3
			{ type = pt.CRAY, x = stack_index * 3, ctype = pt.SPRK, tmp2 = stack_index * 3 },
			--          fi    p3
		}
	end,
	[ "east" ] = function(ctx, stack_index)
		return {
			--          fi co ?3
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in co ?3
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          in co fi
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          ps co fi
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          p4 ld fi
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = ctx.left13 * 2 + 3 },
			--          p4 ls fi
			{ type = pt.LSNS, x = stack_index * 3, tmp = 3, tmp2 = 1 },
			--          p3    fi
		}
	end,
	[ "west" ] = function(ctx, stack_index)
		return {
			--          p3 co fi
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          p3 co in
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          fi co in
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          fi co ps
			{ type = pt.CONV, x = stack_index * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          fi ld p4
			{ type = pt.LDTC, x = stack_index * 3, tmp = 1, life = ctx.right13 - 1 + (ctx.stacks - stack_index) * 3 },
			--          fi ls p4
			{ type = pt.LSNS, x = stack_index * 3, tmp = 3, tmp2 = 1 },
			--          fi    p3
		}
	end,
}

local step_type_to_macro = {
	[  0 ] = { name = "load"  , params = { "stack_index", "work_slot", "storage_slot"      } },
	[  1 ] = { name = "cload" , params = { "stack_index", "work_slot"                      } },
	[  2 ] = { name = "mode"  , params = { "stack_index", "tmp"                            } },
	[  3 ] = { name = "store" , params = { "stack_index", "work_slot", "storage_slot"      } },
	[  4 ] = { name = "cstore", params = { "stack_index", "work_slot", "storage_slot"      } },
	[  5 ] = { name = "aray"  , params = { "stack_index"                                   } },
	[  6 ] = { name = "east"  , params = { "stack_index"                                   } },
	[  7 ] = { name = "west"  , params = { "stack_index"                                   } },
	[  8 ] = { name = "clear" , params = { "stack_index"                                   } },
	[  9 ] = { name = "top"   , params = { "stack_index"                                   } },
	[ 10 ] = { name = "bottom", params = { "stack_index"                                   } },
	[ 11 ] = { name = "lcap"  , params = { "stack_index", "life3_index"                    } },
	[ 12 ] = { name = "lfilt" , params = { "stack_index", "work_slot"                      } },
	[ 13 ] = { name = "rfilt" , params = { "stack_index", "storage_slot", "constant_value" } },
}
local step_mname_to_macro = {}
for _, value in pairs(step_type_to_macro) do
	step_mname_to_macro[value.name] = value
end

local function parse_numbers(ctx, data)
	local take_number
	do
		local numbers = {}
		for word in data:gmatch("%S+") do
			table.insert(numbers, assert(tonumber(word)))
		end
		local cursor = 1
		function take_number()
			local number = assert(numbers[cursor])
			cursor = cursor + 1
			return number
		end
	end
	local stacks = take_number()
	local steps = take_number()
	local cost = take_number()
	ctx.stacks = stacks
	ctx.cost   = cost
	ctx.left13 = 0
	local calls = {}
	for i = 1, steps do
		local stack = take_number()
		assert(info.params[1] == "stack_index")
		local step_type = take_number()
		local info = assert(step_type_to_macro[step_type])
		local func = assert(particle_macros[info.name])
		local params = { func, ctx }
		for j = 1, #info.params do
			table.insert(params, take_number())
		end
		if info.name == "lfilt" then
			ctx.left13 = ctx.left13 + 1
		end
		if info.name == "rfilt" and params[5] == build.LSNS_LIFE_3 then
			ctx.right13 = params[4]
		end
		table.insert(calls, params)
	end
	return calls
end

local function parse_plan(ctx, plan)
	ctx.stacks = plan.stack_count
	ctx.cost   = plan.part_count
	local calls = {}
	for i = 1, #plan.steps do
		local step = plan.steps[i]
		local info = assert(step_mname_to_macro[step.type])
		local func = assert(particle_macros[info.name])
		local params = { func, ctx }
		for j = 1, #info.params do
			table.insert(params, step[info.params[j]] or false)
		end
		if info.name == "lfilt" then
			ctx.left13 = params[4] + 1
		end
		if info.name == "rfilt" and params[5] == build.LSNS_LIFE_3 then
			ctx.right13 = params[4]
		end
		table.insert(calls, params)
	end
	return calls
end

local function plot(x, y, data, extra)
	local ctx = {}
	local calls
	if getmetatable(data) == optimize.state_mt then
		calls = parse_plan(ctx, data:plan())
	else
		calls = parse_numbers(ctx, data)
	end
	local parts = {}
	for i = 1, #calls do
		local func = calls[i][1]
		for _, part in ipairs(func(unpack(calls[i], 2))) do
			table.insert(parts, part)
		end
	end
	assert(#parts == ctx.cost)
	for ix_stack = 0, ctx.stacks - 1 do
		sim.createWalls(x + ix_stack * 3, y, 1, 1, 12)
	end
	if extra then
		for i = 1, #extra do
			table.insert(parts, extra[i])
		end
	end
	for i = 1, #parts do
		parts[i].k = i
		parts[i].x = parts[i].x or 0
		parts[i].y = parts[i].y or 0
	end
	table.sort(parts, function(lhs, rhs)
		if lhs.y ~= rhs.y then return lhs.y < rhs.y end
		if lhs.x ~= rhs.x then return lhs.x < rhs.x end
		if lhs.k ~= rhs.k then return lhs.k < rhs.k end
		return false
	end)
	for i = 1, #parts do
		local id = sim.partCreate(-3, 4, 4, pt.DMND)
		if id == -1 then
			for j = 1, i - 1 do
				sim.partKill(parts[j].id)
			end
			error("out of particle ids", 2)
		end
		parts[i].x = parts[i].x + x
		parts[i].y = parts[i].y + y
		sim.partProperty(id, "type", parts[i].type)
		for key, value in pairs(parts[i]) do
			if sim["FIELD_" .. key:upper()] and key ~= "type" then
				sim.partProperty(id, key, value)
			end
		end
	end
end

return {
	plot = plot,
}
