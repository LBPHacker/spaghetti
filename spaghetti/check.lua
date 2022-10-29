local function number(entity, level, thing)
	level = level + 1
	if type(thing) ~= "number" then
		error(entity .. " is not a number", level)
	end
end

local function integer(entity, level, thing)
	level = level + 1
	number(entity, level, thing)
	if thing % 1 ~= 0 then
		error(entity .. " is not an integer", level)
	end
end

local function positive_integer(entity, level, thing)
	level = level + 1
	integer(entity, level, thing)
	if thing <= 0 then
		error(entity .. " is out of range", level)
	end
end

local function limited_number(entity, level, thing, lower, upper)
	level = level + 1
	number(entity, level, thing)
	if thing < lower or thing >= upper then
		error(entity .. " is out of range", level)
	end
end

local function limited_integer(entity, level, thing, lower, upper)
	level = level + 1
	integer(entity, level, thing)
	if thing < lower or thing >= upper then
		error(entity .. " is out of range", level)
	end
end

local function part_type(entity, level, thing)
	level = level + 1
	positive_integer(entity, level, thing)
	if elem.exists(thing) then
		error(entity .. " is not a valid element ID", level)
	end
end

local function part_life(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x10000)
end

local function part_ctype(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x100000000)
end

local function part_x(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, sim.XRES)
end

local function part_y(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, sim.YRES)
end

local function part_vx(entity, level, thing)
	limited_number(entity, level + 1, thing, -10000, 10000)
end

local function part_vy(entity, level, thing)
	limited_number(entity, level + 1, thing, -10000, 10000)
end

local function part_temp(entity, level, thing)
	limited_number(entity, level + 1, thing, 0, 10000)
end

local function part_tmp(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x100000000)
end

local function part_tmp2(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x10000)
end

local function part_tmp3(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x100000000)
end

local function part_tmp4(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x100000000)
end

local function part_dcolour(entity, level, thing)
	limited_integer(entity, level + 1, thing, 0, 0x100000000)
end

local function table_(entity, level, thing)
	level = level + 1
	if type(thing) ~= "table" then
		error(entity .. " is not a table", level)
	end
end

return {
	number = number,
	integer = integer,
	positive_integer = positive_integer,
	limited_integer = limited_integer,
	part_type = part_type,
	part_life = part_life,
	part_ctype = part_ctype,
	part_x = part_x,
	part_y = part_y,
	part_vx = part_vx,
	part_vy = part_vy,
	part_temp = part_temp,
	part_tmp = part_tmp,
	part_tmp2 = part_tmp2,
	part_tmp3 = part_tmp3,
	part_tmp4 = part_tmp4,
	part_dcolour = part_dcolour,
	table = table_,
}
