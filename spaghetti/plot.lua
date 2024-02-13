local strict = require("spaghetti.strict")
strict.wrap_env()

local build = require("spaghetti.build")

local pt = setmetatable({}, { __index = function(tbl, key)
	return elem["DEFAULT_PT_" .. key]
end })

local particle_macros = {
	[ "lcap" ] = function(ctx, stack)
		return {
			{ type = pt.FILT, x = -ctx.LEFT_13 * 2 - 4, ctype = build.LSNS_LIFE_3 },
			{ type = pt.DMND, x = -ctx.LEFT_13 * 2 - 3 },
		}
	end,	
	[ "lfilt" ] = function(ctx, stack, offset)
		return {
			{ type = pt.FILT, x = -offset * 2 - 3 },
		}
	end,	
	[ "rfilt" ] = function(ctx, stack, offset, value)
		return {
			{ type = pt.FILT, x = offset + ctx.STACKS * 3, ctype = value },
		}
	end,	
	[ "bottom" ] = function(ctx, stack)
		return {
			{ type = pt.FILT, x = stack * 3 - 1 },
			{ type = pt.CONV, x = stack * 3 + 1, tmp = pt.SPRK, ctype = pt.PSCN },
			{ type = pt.CONV, x = stack * 3 + 1, tmp = pt.PSCN, ctype = pt.SPRK },
			{ type = pt.SPRK, x = stack * 3 + 1, ctype = pt.PSCN, life = 4 },
		}
	end,
	[ "top" ] = function(ctx, stack)
		return {
			{ type = pt.STOR, x = stack * 3 },
		}
	end,	
	[ "mode" ] = function(ctx, stack, tmp)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in co p3
			{ type = pt.CONV, x = stack * 3, tmp = pt.INSL, ctype = bit.bor(pt.FILT, bit.lshift(tmp, sim.PMAPBITS)) },
			--          fi    p3
		}
	end,
	[ "load" ] = function(ctx, stack, to, from)
		return {
			--          fi ld p3
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from - 1 + (ctx.STACKS - stack) * 3 },
			--          fi dr p3
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to * 2 + 1 + stack * 3 },
			--          fi    p3
		}
	end,
	[ "cload" ] = function(ctx, stack, to)
		return {
			--          fi dr p3
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to * 2 + 1 + stack * 3 },
			--          fi    p3
		}
	end,
	[ "cstore" ] = function(ctx, stack, from, to)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from * 2 + 2 + stack * 3 },
			--          p3    fi
		}
	end,
	[ "store" ] = function(ctx, stack, from, to)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from * 2 + 2 + stack * 3 },
			--          p3 dr fi
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to - 2 + (ctx.STACKS - stack) * 3 },
			--          p3    fi
		}
	end,
	[ "aray" ] = function(ctx, stack)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.INST },
			--          fi co it
			{ type = pt.CONV, x = stack * 3, tmp = pt.INST, ctype = pt.SPRK },
			--          fi ld i4
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.RIGHT_13 - 1 + (ctx.STACKS - stack) * 3 },
			--          fi ls i4
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          fi ar i3
			{ type = pt.ARAY, x = stack * 3 },
			--          fi    i3
		}
	end,
	[ "clear" ] = function(ctx, stack)
		return {
			--          fi cr p3
			{ type = pt.CRAY, x = stack * 3, ctype = pt.SPRK, tmp2 = stack * 3 - 3 },
			--          fi    p3
		}
	end,
	[ "east" ] = function(ctx, stack)
		return {
			--          fi co ?3
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in co ?3
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          in co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          ps co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          p4 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.LEFT_13 * 2 + 3 },
			--          p4 ls fi
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          p3    fi
		}
	end,
	[ "west" ] = function(ctx, stack)
		return {
			--          p3 co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          p3 co in
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          fi co in
			{ type = pt.CONV, x = stack * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          fi co ps
			{ type = pt.CONV, x = stack * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          fi ld p4
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.RIGHT_13 - 1 + (ctx.STACKS - stack) * 3 },
			--          fi ls p4
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          fi    p3
		}
	end,
}

local step_type_to_macro = {
	[  0 ] = { name = "load"  , params = 2 },
	[  1 ] = { name = "cload" , params = 1 },
	[  2 ] = { name = "mode"  , params = 1 },
	[  3 ] = { name = "store" , params = 2 },
	[  4 ] = { name = "cstore", params = 2 },
	[  5 ] = { name = "aray"  , params = 0 },
	[  6 ] = { name = "east"  , params = 0 },
	[  7 ] = { name = "west"  , params = 0 },
	[  8 ] = { name = "clear" , params = 0 },
	[  9 ] = { name = "top"   , params = 0 },
	[ 10 ] = { name = "bottom", params = 0 },
	[ 11 ] = { name = "lcap"  , params = 1 },
	[ 12 ] = { name = "lfilt" , params = 1 },
	[ 13 ] = { name = "rfilt" , params = 2 },
}

local function plot(x, y, data, extra)
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
	local parts = {}
	local stacks = take_number()
	local ctx = {
		STACKS = stacks,
		LEFT_13 = 0,
	}
	local steps = take_number()
	local cost = take_number()
	local calls = {}
	for i = 1, steps do
		local stack = take_number()
		local step_type = take_number()
		local info = assert(step_type_to_macro[step_type])
		local func = assert(particle_macros[info.name])
		local params = { func, ctx, stack }
		for j = 1, info.params do
			table.insert(params, take_number())
		end
		if info.name == "lfilt" then
			ctx.LEFT_13 = ctx.LEFT_13 + 1
		end
		if info.name == "rfilt" and params[5] == build.LSNS_LIFE_3 then
			ctx.RIGHT_13 = params[4]
		end
		table.insert(calls, params)
	end
	for i = 1, #calls do
		local func = calls[i][1]
		for _, part in ipairs(func(unpack(calls[i], 2))) do
			table.insert(parts, part)
		end
	end
	assert(#parts == cost)
	for ix_stack = 0, stacks - 1 do
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
