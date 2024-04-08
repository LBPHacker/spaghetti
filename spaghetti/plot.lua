local strict = require("spaghetti.strict")
strict.wrap_env()

local build    = require("spaghetti.build")
local optimize = require("spaghetti.optimize")
local bitx     = require("spaghetti.bitx")
local misc     = require("spaghetti.misc")
local check    = require("spaghetti.check")

local audited_pairs = pairs

local in_tpt = rawget(_G, "tpt") and true

local elem_mt = {}
function elem_mt:__tostring()
	return ("pt.%s"):format(self.key)
end
local pt = setmetatable({}, { __index = function(tbl, key)
	if in_tpt then
		return elem["DEFAULT_PT_" .. key]
	end
	local value = setmetatable({ key = key }, elem_mt)
	rawset(tbl, key, value)
	return value
end })

local particle_macros = {
	[ "lcap" ] = {
		params = { "stack_index", "life3_index" },
		func = function(ctx, stack_index, life3_index)
			return {
				{ type = pt.FILT, x = -ctx.left13 * 2 - 4, ctype = build.LSNS_LIFE_3 },
				{ type = pt.DMND, x = -ctx.left13 * 2 - 3 },
			}
		end,
	},
	[ "lfilt" ] = {
		params = { "stack_index", "work_slot" },
		func = function(ctx, stack_index, work_slot)
			return {
				{ type = pt.FILT, x = -work_slot * 2 - 3 },
			}
		end,
	},
	[ "rfilt" ] = {
		params = { "stack_index", "storage_slot", "constant_value" },
		func = function(ctx, stack_index, storage_slot, constant_value)
			return {
				{ type = pt.FILT, x = storage_slot + ctx.stacks * 2 + 1, ctype = constant_value },
			}
		end,
	},
	[ "bottom" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			if stack_index % 2 == 1 then
				return {
					--          p4 co fi
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.FILT, ctype = pt.INSL },
					--          p4 co in
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.SPRK, ctype = pt.FILT },
					--          fi co in
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.INSL, ctype = pt.PSCN },
					--          fi co ps
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.PSCN, ctype = pt.SPRK },
					--          fi ld p4
					{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = ctx.right13 + (ctx.stacks - stack_index) * 2 },
					--          fi ls p4
					{ type = pt.LSNS, x = stack_index * 2, tmp = 3, tmp2 = 1, print_index = 1 },
					--          fi    p3
					{ type = pt.FILT, x = stack_index * 2 + 1 },
					stack_index == 0 and { type = pt.CONV, x = stack_index * 2 - 1, tmp = pt.SPRK, ctype = pt.PSCN } or nil,
					stack_index == 0 and { type = pt.CONV, x = stack_index * 2 - 1, tmp = pt.PSCN, ctype = pt.SPRK } or nil,
					stack_index == 0 and { type = pt.SPRK, x = stack_index * 2 - 1, ctype = pt.PSCN, life = 4 }      or nil,
				}
			end
			return {
				{ type = pt.CONV, x = stack_index * 2 + 1, tmp = pt.SPRK, ctype = pt.PSCN },
				{ type = pt.CONV, x = stack_index * 2 + 1, tmp = pt.PSCN, ctype = pt.SPRK },
				{ type = pt.SPRK, x = stack_index * 2 + 1, ctype = pt.PSCN, life = 4 },
				stack_index == 0 and { type = pt.FILT, x = stack_index * 2 - 1 } or nil,
			}
		end,
	},
	[ "top" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			if stack_index % 2 == 1 then
				return {
					--          fi co ?3
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.FILT, ctype = pt.INSL },
					--          in co ?3
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.SPRK, ctype = pt.FILT },
					--          in co fi
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.INSL, ctype = pt.PSCN },
					--          ps co fi
					{ type = pt.CONV, x = stack_index * 2, tmp = pt.PSCN, ctype = pt.SPRK },
					--          p4 ld fi
					{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = ctx.left13 * 2 + 3 + stack_index * 2 },
					--          p4    fi
					{ type = pt.STOR, x = stack_index * 2 },
				}
			end
			return {
				{ type = pt.STOR, x = stack_index * 2 },
			}
		end,
	},
	[ "mode" ] = {
		params = { "stack_index", "tmp" },
		func = function(ctx, stack_index, tmp)
			return {
				--          fi co p3
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.FILT, ctype = pt.INSL },
				--          in co p3
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.INSL, ctype = pt.FILT, ctype_high = tmp },
				--          fi    p3
			}
		end,
	},
	[ "load" ] = {
		params = { "stack_index", "work_slot", "storage_slot" },
		func = function(ctx, stack_index, work_slot, storage_slot)
			return {
				--          fi ld p3
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = storage_slot + (ctx.stacks - stack_index) * 2 },
				--          fi dr p3
				{ type = pt.DRAY, x = stack_index * 2, tmp = 1, tmp2 = work_slot * 2 + 1 + stack_index * 2 },
				--          fi    p3
			}
		end,
	},
	[ "cload" ] = {
		params = { "stack_index", "work_slot" },
		func = function(ctx, stack_index, work_slot)
			return {
				--          fi dr p3
				{ type = pt.DRAY, x = stack_index * 2, tmp = 1, tmp2 = work_slot * 2 + 1 + stack_index * 2 },
				--          fi    p3
			}
		end,
	},
	[ "cstore" ] = {
		params = { "stack_index", "work_slot", "storage_slot" },
		func = function(ctx, stack_index, work_slot, storage_slot)
			return {
				--          p3 ld fi
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = work_slot * 2 + 3 + stack_index * 2 },
				--          p3    fi
			}
		end,
	},
	[ "store" ] = {
		params = { "stack_index", "work_slot", "storage_slot" },
		func = function(ctx, stack_index, work_slot, storage_slot)
			return {
				--          p3 ld fi
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = work_slot * 2 + 3 + stack_index * 2 },
				--          p3 dr fi
				{ type = pt.DRAY, x = stack_index * 2, tmp = 1, tmp2 = storage_slot - 1 + (ctx.stacks - stack_index) * 2 },
				--          p3    fi
			}
		end,
	},
	[ "aray" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			return {
				--          fi co p3
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.SPRK, ctype = pt.INST },
				--          fi co it
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.INST, ctype = pt.SPRK },
				--          fi ld i4
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = ctx.right13 + (ctx.stacks - stack_index) * 2 },
				--          fi ls i4
				{ type = pt.LSNS, x = stack_index * 2, tmp = 3, tmp2 = 1 },
				--          fi ar i3
				{ type = pt.ARAY, x = stack_index * 2 },
				--          fi    i3
			}
		end,
	},
	[ "clear" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			return {
				--          fi cr p3
				{ type = pt.CRAY, x = stack_index * 2, ctype = pt.SPRK, tmp2 = stack_index * 2 },
				--          fi    p3
			}
		end,
	},
	[ "east" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			return {
				--          fi co ?3
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.FILT, ctype = pt.INSL },
				--          in co ?3
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.SPRK, ctype = pt.FILT },
				--          in co fi
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.INSL, ctype = pt.PSCN },
				--          ps co fi
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.PSCN, ctype = pt.SPRK },
				--          p4 ld fi
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = ctx.left13 * 2 + 3 + stack_index * 2 },
				--          p4 ls fi
				{ type = pt.LSNS, x = stack_index * 2, tmp = 3, tmp2 = 1 },
				--          p3    fi
			}
		end,
	},
	[ "west" ] = {
		params = { "stack_index" },
		func = function(ctx, stack_index)
			return {
				--          p3 co fi
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.FILT, ctype = pt.INSL },
				--          p3 co in
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.SPRK, ctype = pt.FILT },
				--          fi co in
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.INSL, ctype = pt.PSCN },
				--          fi co ps
				{ type = pt.CONV, x = stack_index * 2, tmp = pt.PSCN, ctype = pt.SPRK },
				--          fi ld p4
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = ctx.right13 + (ctx.stacks - stack_index) * 2 },
				--          fi ls p4
				{ type = pt.LSNS, x = stack_index * 2, tmp = 3, tmp2 = 1, print_index = 1 },
				--          fi    p3
			}
		end,
	},
}

local function parse_plan(ctx, plan)
	ctx.stacks = plan.stacks
	ctx.cost   = plan.part_count
	local calls = {}
	for i = 1, #plan.steps do
		local step = plan.steps[i]
		local info = assert(particle_macros[step.type])
		local params = { info.func, ctx }
		for j = 1, #info.params do
			local key = info.params[j]
			local value = step[key] or false
			table.insert(params, value)
		end
		if step.type == "lfilt" then
			ctx.left13 = params[4] + 1
		end
		if step.type == "rfilt" and params[5] == build.LSNS_LIFE_3 then
			ctx.right13 = params[4]
		end
		table.insert(calls, params)
	end
	return calls
end

local function apply_order(parts_in)
	local parts = {}
	for i = 1, #parts_in do
		parts[i] = {}
		for key, value in audited_pairs(parts_in[i]) do
			parts[i][key] = value
		end
		parts[i].z = parts[i].z or i
	end
	table.sort(parts, function(lhs, rhs)
		if lhs.y ~= rhs.y then return lhs.y < rhs.y end
		if lhs.x ~= rhs.x then return lhs.x < rhs.x end
		if lhs.z ~= rhs.z then return lhs.z < rhs.z end
		return false
	end)
	for i = 1, #parts do
		parts[i].z = nil
	end
	return parts
end

local function merge_parts(x, y, parts_out, parts_in)
	return misc.user_wrap(function()
		check.integer("x", x)
		check.integer("y", y)
		local parts = apply_order(parts_in)
		for i = 1, #parts do
			parts[i].x = parts[i].x + x
			parts[i].y = parts[i].y + y
			table.insert(parts_out, parts[i])
		end
		return parts_out
	end)
end

local function create_parts_(x, y, parts_in, debug)
	check.integer("x", x)
	check.integer("y", y)
	local parts = apply_order(parts_in)
	for i = 1, #parts do
		parts[i].x = parts[i].x + x
		parts[i].y = parts[i].y + y
		if parts[i].ctype_high then
			parts[i].ctype = bitx.bor(parts[i].ctype, bitx.lshift(parts[i].ctype_high, sim.PMAPBITS))
		end
	end
	do
		local new_parts = {}
		local parts_by_pos = {}
		local function xy_key(x, y)
			return y * sim.XRES + x
		end
		for _, part in ipairs(parts) do
			local key = xy_key(part.x, part.y)
			local insert = true
			if parts_by_pos[key] then
				if part.unstack then
					insert = false
				end
			else
				parts_by_pos[key] = part
			end
			if insert then
				table.insert(new_parts, part)
			end
		end
		parts = new_parts
	end
	local ids = {}
	for i = 1, #parts do
		local id = sim.partCreate(-3, 4, 4, pt.DMND)
		if id == -1 then
			for j = 1, i - 1 do
				sim.partKill(ids[j])
			end
			error("out of particle ids", 2)
		end
		ids[i] = id
	end
	table.sort(ids)
	local function xy_key(x, y)
		return y * sim.XRES + x
	end
	local function xy_key_back(k)
		return k % sim.XRES, math.floor(k / sim.XRES)
	end
	local count_at = {}
	for i = 1, #parts do
		if debug and parts[i].print_index then
			print("layer_last", ids[i])
		end
		sim.partProperty(ids[i], "type", parts[i].type)
		for key, value in audited_pairs(parts[i]) do
			if sim["FIELD_" .. key:upper()] and key ~= "type" then
				sim.partProperty(ids[i], key, value)
			end
		end
		local key = xy_key(parts[i].x, parts[i].y)
		count_at[key] = (count_at[key] or 0) + 1
	end
	for key, value in audited_pairs(count_at) do
		if value > 5 then
			local x, y = xy_key_back(key)
			sim.createWalls(x, y, 1, 1, sim.walls.DEFAULT_WL_EHOLE)
		end
	end
end

local function create_parts(x, y, parts_in, debug)
	return misc.user_wrap(function()
		return create_parts_(x, y, parts_in, debug)
	end)
end

local function partsify_plan(plan, extra)
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

local function plan(x, y, plan, extra, debug)
	return misc.user_wrap(function()
		local parts = partsify_plan(plan, extra)
		create_parts_(x, y, parts, debug)
	end)
end

local function serialize_parts_(parts_in, seed)
	local parts = apply_order(parts_in)
	local arr = {}
	table.insert(arr, ([[
-- this file was generated by spaghetti, do not edit it manually
-- seed: 0x%08X 0x%08X

local pt = require("spaghetti.plot").pt

return {]]):format(seed[1], seed[2]))
	for i = 1, #parts do
		local words = {}
		for key, value in audited_pairs(parts[i]) do
			table.insert(words, ("%s = %s"):format(key, tostring(value)))
		end
		table.insert(arr, ("\t{ %s },"):format(table.concat(words, ", ")))
	end
	table.insert(arr, [[
}
]])
	return table.concat(arr, "\n")
end

local function serialize_plan(plan, extra, seed)
	return misc.user_wrap(function()
		local parts = partsify_plan(plan, extra)
		return serialize_parts_(parts, seed)
	end)
end

return {
	plan            = plan,
	pt              = pt,
	create_parts    = create_parts,
	serialize_plan  = serialize_plan,
	merge_parts     = merge_parts,
}
