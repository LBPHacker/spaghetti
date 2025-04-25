local bitx       = require("spaghetti.bitx")
local misc       = require("spaghetti.misc")
local check      = require("spaghetti.check")
local modulepack = require("modulepack")

local LSNS_LIFE_3 = 0x10000003
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

local storage_remap_mt = {}
function storage_remap_mt:__tostring()
	return ("setmetatable({ storage_slot = %s, offset = %s }, storage_remap_mt)"):format(tostring(self.storage_slot), tostring(self.offset))
end

local function xy_key(x, y)
	return y * sim.XRES + x
end

local function xy_key_back(k)
	return k % sim.XRES, math.floor(k / sim.XRES)
end

local particle_macros = {
	[ "lcap" ] = {
		params = { "stack_index", "life3_index" },
		func = function(ctx, stack_index, life3_index)
			return {
				{ type = pt.FILT, x = -ctx.left13 * 2 - 4, ctype = LSNS_LIFE_3 },
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
				{ type = pt.FILT, x = setmetatable({ storage_slot = storage_slot, offset = ctx.stacks * 2 + 1 }, storage_remap_mt), ctype = constant_value },
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
				{ type = pt.LDTC, x = stack_index * 2, tmp = 1, life = setmetatable({ storage_slot = storage_slot, offset = (ctx.stacks - stack_index) * 2 }, storage_remap_mt) },
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
				{ type = pt.DRAY, x = stack_index * 2, tmp = 1, tmp2 = setmetatable({ storage_slot = storage_slot, offset = (ctx.stacks - stack_index) * 2 - 1 }, storage_remap_mt) },
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

local function apply_order(parts_in, storage_remap)
	local parts = {}
	for i = 1, #parts_in do
		parts[i] = {}
		for key, value in audited_pairs(parts_in[i]) do
			if storage_remap and getmetatable(value) == storage_remap_mt then
				value = (storage_remap[value.storage_slot] or value.storage_slot) + value.offset
			end
			parts[i][key] = value
		end
		parts[i].z = parts[i].z or i
	end
	local function resolve_storage_remap_x(x)
		if getmetatable(x) == storage_remap_mt then
			x = x.storage_slot + x.offset
		end
		return x
	end
	table.sort(parts, function(lhs, rhs)
		if lhs.y ~= rhs.y then return lhs.y < rhs.y end
		local lhs_x = resolve_storage_remap_x(lhs.x)
		local rhs_x = resolve_storage_remap_x(rhs.x)
		if lhs_x ~= rhs_x then return lhs_x < rhs_x end
		if lhs.z ~= rhs.z then return lhs.z < rhs.z end
		return false
	end)
	for i = 1, #parts do
		parts[i].z = nil
	end
	return parts
end

local merge_parts = misc.user_wrap(function(x, y, parts_out, parts_in, storage_remap)
	check.integer("x", x)
	check.integer("y", y)
	local parts = apply_order(parts_in, storage_remap or {})
	for i = 1, #parts do
		parts[i].x = parts[i].x + x
		parts[i].y = parts[i].y + y
		table.insert(parts_out, parts[i])
	end
	return parts_out
end)

local function create_parts_(x, y, parts_in, storage_remap, debug)
	check.integer("x", x)
	check.integer("y", y)
	local parts = apply_order(parts_in, storage_remap)
	for i = 1, #parts do
		parts[i].x = parts[i].x + x
		parts[i].y = parts[i].y + y
		if parts[i].ctype_high then
			parts[i].ctype = bitx.bor(parts[i].ctype, bitx.lshift(parts[i].ctype_high, sim.PMAPBITS))
		end
	end
	do
		local new_parts = {}
		local part_state = {}
		local function xy_key(x, y)
			return y * sim.XRES + x
		end
		for _, part in ipairs(parts) do
			local key = xy_key(part.x, part.y)
			if not part.unstack then
				part_state[key] = "strong"
			end
		end
		for _, part in ipairs(parts) do
			local key = xy_key(part.x, part.y)
			if not part.unstack or part_state[key] ~= "strong" then
				part_state[key] = "strong"
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
	do
		local to_freeze = {}
		for i = 1, #parts do
			local x, y = math.floor(parts[i].x / sim.CELL), math.floor(parts[i].y / sim.CELL)
			to_freeze[xy_key(x, y)] = true
		end
		for i = 1, #parts do
			if not parts[i].freezable then
				local x, y = math.floor(parts[i].x / sim.CELL), math.floor(parts[i].y / sim.CELL)
				to_freeze[xy_key(x, y)] = nil
			end
		end
		for key in audited_pairs(to_freeze) do
			local x, y = xy_key_back(key)
			sim.createWalls(x * sim.CELL, y * sim.CELL, 1, 1, sim.walls.DEFAULT_WL_STASIS)
		end
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

local create_parts = misc.user_wrap(create_parts_)

local function serialize_parts_(parts_in, seed)
	local parts = apply_order(parts_in)
	local arr = {}
	table.insert(arr, ([[
-- this file was generated by spaghetti, do not edit it manually
-- seed: 0x%08X 0x%08X

local plot = require("spaghetti.plot")
local pt = plot.pt
local storage_remap_mt = plot.storage_remap_mt

local function get_parts(storage_remap)
	return {]]):format(seed[1], seed[2]))
		for i = 1, #parts do
			local words = {}
			for key, value in misc.ordered_pairs(parts[i]) do
				table.insert(words, ("%s = %s"):format(key, tostring(value)))
			end
			table.insert(arr, ("\t\t{ %s },"):format(table.concat(words, ", ")))
		end
		table.insert(arr, [[
	}
end

return {
	get_parts = get_parts,
}
]])
	return table.concat(arr, "\n")
end

local function aftersimdraw_user_stacks(tx, ty, x, y, parts)
	local parts_by_pos = {}
	for _, part in ipairs(parts) do
		if part.user_stack then
			local key = xy_key(part.x, part.y)
			if not parts_by_pos[key] then
				parts_by_pos[key] = {}
			end
			table.insert(parts_by_pos[key], part)
		end
	end
	local cache = {}
	return function()
		local mx, my = sim.adjustCoords(ui.mousePosition())
		local key = xy_key(mx - x, my - y)
		local stack = parts_by_pos[key]
		if stack then
			local line_count = 0
			local function put_line(str)
				local w, h = gfx.textSize(str)
				gfx.fillRect(tx - 5, ty + line_count * 12 - 2, w + 10, h + 2, 0, 0, 0, 192)
				gfx.drawText(tx, ty + line_count * 12, str)
				line_count = line_count + 1
			end
			for j = 1, math.min(#stack, 10) do
				local part = stack[j]
				put_line(("- particle #%i: %s"):format(j, elem.property(part.type, "Name")))
				for i = 1, #part.user_stack do
					if not cache[part.user_stack[i]] then
						local source = part.user_stack[i].source
						local line = part.user_stack[i].currentline
						cache[part.user_stack[i]] = modulepack.demangle(("  - %s:%i"):format(source, line))
					end
					put_line(cache[part.user_stack[i]])
				end
			end
		end
	end
end

local function common_structures(parts, debug_stacks)
	local function sig_magn(x)
		local magn = math.abs(x)
		return x == 0 and 0 or (x / magn), magn
	end

	local function mutate(p, m)
		local q = {}
		for key, value in audited_pairs(p) do
			q[key] = value
		end
		for key, value in audited_pairs(m) do
			q[key] = value
		end
		return q
	end

	local function piston_extend(k)
		if k == math.huge then
			return 10000
		end
		return 273.15 + (k or 0) * 10
	end

	local function part(p)
		local m = {}
		if debug_stacks then
			m.user_stack = misc.user_stack()
		end
		if p.type == pt.PSTN then
			if not p.temp then
				m.temp = piston_extend(p.extend)
			end
			if not p.ctype then
				m.ctype = pt.INSL
			end
			if not p.tmp2 then
				m.tmp2 = 1000
			end
		end
		if p.type == pt.LSNS then
			if not p.tmp2 then
				m.tmp2 = 1
			end
		end
		if p.type == pt.DTEC then
			if not p.tmp2 then
				m.tmp2 = 1
			end
		end
		if p.type == pt.ARAY then
			if not p.life then
				m.life = 1
			end
		end
		if p.type == pt.BRAY then
			if not p.life then
				m.life = 1
			end
		end
		local q = mutate(p, m)
		table.insert(parts, q)
		if q.grvt_cover then
			table.insert(parts, { type = pt.GRVT, x = p.x, y = p.y, life = 0, tmp = 0 })
		end
		return q
	end

	local function spark(p)
		return part(mutate(p, {
			ctype = p.type,
			type = pt.SPRK,
			life = p.life or 4,
		}))
	end

	local solid_spark
	do
		local map = {}
		function solid_spark(x, y, x_off, y_off, conductor, no_auto_z)
			local key = xy_key(x + x_off, y + y_off)
			if map[key] then
				if not (map[key].x == x and map[key].y == y and map[key].conductor == conductor) then
					error("spark conflict", 2)
				end
			else
				part ({ type = pt.CONV  , x = x        , y = y        , tmp = pt.SPRK, ctype = conductor, z = (not no_auto_z) and 10000000 or nil })
				part ({ type = pt.CONV  , x = x        , y = y        , tmp = conductor, ctype = pt.SPRK, z = (not no_auto_z) and 10000001 or nil })
				spark({ type = conductor, x = x + x_off, y = y + y_off })
				map[key] = {
					x = x,
					y = y,
					conductor = conductor,
				}
			end
		end
	end

	local lsns_taboo
	do
		local dmnds = {}
		function lsns_taboo(x, y)
			local key = xy_key(x, y)
			if not dmnds[key] then
				dmnds[key] = part({ type = pt.DMND, x = x, y = y })
			end
			return dmnds[key]
		end
	end

	local lsns_spark
	do
		local lmap = {}
		local function lsns(p)
			local key = xy_key(p.x, p.y)
			if not lmap[key] then
				lmap[key] = true
				part(mutate(p, { type = pt.LSNS, tmp = 3 }))
			end
		end
		local fmap = {}
		local function filt(p, life)
			local key = xy_key(p.x, p.y)
			if not fmap[key] then
				fmap[key] = life
				part(mutate(p, { type = pt.FILT, ctype = 0x10000000 + life }))
			else
				if fmap[key] ~= life then
					error("lsns spark conflict", 3)
				end
			end
		end
		function lsns_spark(p, x_l_off, y_l_off, x_f_off, y_f_off)
			assert(p and x_l_off and y_l_off and x_f_off and y_f_off)
			spark(p)
			lsns({ x = p.x + x_l_off, y = p.y + y_l_off })
			filt({ x = p.x + x_f_off, y = p.y + y_f_off }, p.life)
		end
	end

	local function dray(x, y, x_to, y_to, count, conductor, z, no_auto_z)
		assert(x and y and x_to and y_to and count)
		local dx_sig, dx_magn = sig_magn(x_to - x)
		local dy_sig, dy_magn = sig_magn(y_to - y)
		if not (dx_magn == dy_magn or dx_magn == 0 or dy_magn == 0) then
			error("bad offset", 2)
		end
		local magn = math.max(dx_magn, dy_magn)
		local dist = magn - count - 1
		if dist < 0 then
			error("bad distance", 2)
		end
		local q = part({ type = pt.DRAY, x = x, y = y, tmp = count, tmp2 = dist, z = z })
		if conductor ~= false then
			assert(conductor)
			solid_spark(x, y, -dx_sig, -dy_sig, conductor, no_auto_z)
		end
		return q
	end

	local function dray_log(x, y, x_to, y_to, count, conductor)
		assert(x and y and x_to and y_to and count)
		if conductor ~= false then
			assert(conductor)
		end
		local dx_sig, dx_magn = sig_magn(x_to - x)
		local dy_sig, dy_magn = sig_magn(y_to - y)
		local order = 0
		local dist = math.max(dx_magn, dy_magn)
		local step = dist - 1
		while count > 0 do
			local max_take = bitx.lshift(step, order)
			local take = math.min(max_take, count)
			local x_to = x + dx_sig * dist
			local y_to = y + dy_sig * dist
			dray(x, y, x_to, y_to, take, conductor)
			count = count - take
			dist = dist + take
			order = order + 1
		end
	end

	local function ldtc(x, y, x_to, y_to, z, tmp)
		assert(x and y and x_to and y_to)
		local dx_sig, dx_magn = sig_magn(x_to - x)
		local dy_sig, dy_magn = sig_magn(y_to - y)
		if not (dx_magn == dy_magn or dx_magn == 0 or dy_magn == 0) then
			error("bad offset", 2)
		end
		local magn = math.max(dx_magn, dy_magn)
		local q = part({ type = pt.LDTC, x = x, y = y, life = magn - 1, z = z, tmp = tmp })
		return q
	end

	local function cray(x, y, x_to, y_to, ptype, count, conductor, z, life)
		assert(x and y and x_to and y_to and ptype and count)
		local dx_sig, dx_magn = sig_magn(x_to - x)
		local dy_sig, dy_magn = sig_magn(y_to - y)
		if not (dx_magn == dy_magn or dx_magn == 0 or dy_magn == 0) then
			error("bad offset", 2)
		end
		local magn = math.max(dx_magn, dy_magn)
		local q = part({ type = pt.CRAY, x = x, y = y, ctype = ptype, tmp = count, tmp2 = magn - 1, z = z, life = life })
		if conductor ~= false then
			assert(conductor)
			solid_spark(x, y, -dx_sig, -dy_sig, conductor)
		end
		return q
	end

	local function pos_sort(pos)
		table.sort(pos, function(a, b)
			if a.y ~= b.y then return a.y < b.y end
			if a.x ~= b.x then return a.x < b.x end
			return false
		end)
		return pos
	end

	local function spark_row(x, y, x_to, y_to, conductor, count, life, dist)
		dist = dist or 3
		assert(x and y and x_to and y_to and count and life)
		local dx_sig, dx_magn = sig_magn(x_to - x)
		local dy_sig, dy_magn = sig_magn(y_to - y)
		if not (dx_magn == dy_magn or dx_magn == 0 or dy_magn == 0) then
			error("bad offset", 2)
		end
		local pos = pos_sort({
			{ x = x - dist * dx_sig, y = y - dist * dy_sig },
			{ x = x                , y = y                 },
		})
		cray(pos[1].x, pos[1].y, x_to, y_to, conductor, count, pt.PSCN)
		cray(pos[1].x, pos[1].y, x_to, y_to, conductor, count, pt.PSCN)
		cray(pos[2].x, pos[2].y, x_to, y_to, pt.SPRK, count, pt.INWR, nil, life)
	end

	local function aray(x, y, x_off, y_off, conductor, z, life, no_auto_z)
		assert(x and y and x_off and y_off)
		local q = part({ type = pt.ARAY, x = x, y = y, z = z, life = life })
		if conductor ~= false then
			assert(conductor)
			solid_spark(x, y, x_off, y_off, conductor, no_auto_z)
		end
		return q
	end

	local function frame(x1, y1, x2, y2, bevel_begin, bevel_end)
		bevel_begin = bevel_begin or -2
		bevel_end = bevel_end or 0
		local parts_by_pos = {}
		for _, part in ipairs(parts) do
			parts_by_pos[xy_key(part.x, part.y)] = part
			if not part.dcolour then
				part.dcolour = 0xFF3F3F3F
			end
		end
		local function add_dmnd(x, y)
			local key = xy_key(x, y)
			local q = parts_by_pos[key]
			if q then
				if q.type == pt.FILT  then
					q.dcolour = 0xFF00FFFF
				end
				if q.type == pt.LDTC then
					q.dcolour = 0xFF007F7F
				end
			else
				parts_by_pos[key] = part({ type = pt.DMND, x = x, y = y, dcolour = 0xFFFFFFFF })
			end
		end
		for x = x1 + 1, x2 - 1 do
			add_dmnd(x, y1)
			add_dmnd(x, y1 - 1)
			add_dmnd(x, y2)
			add_dmnd(x, y2 + 1)
		end
		for y = y1 + 1, y2 - 1 do
			add_dmnd(x1, y)
			add_dmnd(x1 - 1, y)
			add_dmnd(x2, y)
			add_dmnd(x2 + 1, y)
		end
		for y = -1, 1 do
			for x = -1, 1 do
				if x + y >= bevel_begin and x + y <= bevel_end then
					add_dmnd(x1 - x, y1 - y)
					add_dmnd(x1 - x, y2 + y)
					add_dmnd(x2 + x, y1 - y)
					add_dmnd(x2 + x, y2 + y)
				end
			end
		end
		return parts_by_pos
	end

	return {
		sig_magn      = sig_magn,
		mutate        = mutate,
		piston_extend = piston_extend,
		part          = part,
		spark         = spark,
		solid_spark   = solid_spark,
		lsns_taboo    = lsns_taboo,
		lsns_spark    = lsns_spark,
		dray          = dray,
		dray_log      = dray_log,
		ldtc          = ldtc,
		cray          = cray,
		aray          = aray,
		spark_row     = spark_row,
		frame         = frame,
	}
end

return {
	pt                       = pt,
	create_parts             = create_parts,
	create_parts_            = create_parts_,
	merge_parts              = merge_parts,
	serialize_parts_         = serialize_parts_,
	storage_remap_mt         = storage_remap_mt,
	xy_key                   = xy_key,
	xy_key_back              = xy_key_back,
	particle_macros          = particle_macros,
	LSNS_LIFE_3              = LSNS_LIFE_3,
	common_structures        = common_structures,
	aftersimdraw_user_stacks = aftersimdraw_user_stacks,
}
