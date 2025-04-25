local bitx = require("spaghetti.bitx")

local forward_frame_name = "=[spaghetti_forward_frame]"
local user_wrap_chunk = loadstring([[
	local function packn(...)
		return { select("#", ...), ... }
	end
	local params_in = packn(...)
	local params_out = packn(params_in[2](unpack(params_in, 3, params_in[1] + 1)))
	return unpack(params_out, 2, params_out[1] + 1)
]], forward_frame_name)

local function user_wrap(func)
	return function(...)
		return user_wrap_chunk(func, ...)
	end
end

local audited_pairs = pairs

local function user_error(...)
	local level = 1
	while true do
		local info = assert(debug.getinfo(level), "cannot find the topmost user frame")
		if info.source == forward_frame_name then
			break
		end
		level = level + 1
	end
	level = level + 1
	while true do
		local info = assert(debug.getinfo(level), "cannot find the topmost user frame")
		if info.source ~= forward_frame_name then
			error(string.format(...), level)
		end
		level = level + 1
	end
end

local function user_stack()
	local stack = {}
	local level = 2
	while true do
		local info = assert(debug.getinfo(level), "cannot find the topmost user frame")
		if info.source == forward_frame_name then
			break
		end
		table.insert(stack, info)
		level = level + 1
	end
	return stack
end

local function user_frame_name()
	local _, err = pcall(user_error, "@")
	return err:match("^(.*): @$") or "[cannot determine user frame name]"
end

local function parse_package_config()
	local line_counter = 0
	local path_delim, spath_delim, module_macro
	for line in package.config:gmatch("([^\r\n]*)\r?\n") do
		line_counter = line_counter + 1
		if line_counter == 1 then
			path_delim = line
		end
		if line_counter == 2 then
			spath_delim = line
		end
		if line_counter == 3 then
			module_macro = line
		end
	end
	if not (path_delim and spath_delim and module_macro) then
		return
	end
	return path_delim, spath_delim, module_macro
end

local function good_split(str, delim)
	local cursor = 1
	local arr = {}
	while true do
		local next_cursor = str:find(delim, cursor, true)
		if not next_cursor then
			break
		end
		table.insert(arr, str:sub(cursor, next_cursor - 1))
		cursor = next_cursor + #delim
	end
	table.insert(arr, str:sub(cursor))
	return arr
end

local function call_site_name(frame_name)
	local path_delim, spath_delim, module_macro = parse_package_config()
	if not path_delim then
		return
	end
	local source, line = frame_name:match("^(.*):(%d+)$")
	if not source then
		return
	end
	for _, path in ipairs(good_split(package.path, spath_delim)) do
		local common_prefix = 0
		for i = 1, #source do
			if source:sub(i, i) ~= path:sub(i, i) then
				break
			end
			common_prefix = i
		end
		local common_suffix = 0
		for i = -1, -(#source - common_prefix), -1 do
			if source:sub(i, i) ~= path:sub(i, i) then
				break
			end
			common_suffix = i
		end
		local source_between = source:sub(common_prefix + 1, #source + common_suffix)
		local path_between = path:sub(common_prefix + 1, #path + common_suffix)
		if path_between == module_macro then
			local components = good_split(source_between, path_delim)
			local modname = table.concat(components, ".")
			return ("%s:%i"):format(modname, line)
		end
	end
end

local function shared_key(one, other)
	for key in audited_pairs(one) do
		if other[key] then
			return key
		end
	end
end

local function reverse(arr)
	local res = {}
	for i = #arr, 1, -1 do
		table.insert(res, arr[i])
	end
	return res
end

local function linked_to_array(linked)
	local res = {}
	while linked do
		table.insert(res, linked)
		linked = linked.next
	end
	return res
end

local function fnv1a32(data)
	local hash = 2166136261
	for i = 1, #data do
		hash = bit.bxor(hash, data:byte(i))
		hash = bit.band(bit.lshift(hash, 24), 0xFFFFFFFF) + bit.band(bit.lshift(hash, 8), 0xFFFFFFFF) + hash * 147
	end
	hash = bit.band(hash, 0xFFFFFFFF)
	return hash < 0 and (hash + 0x100000000) or hash
end

local function crappy_seed()
	math.randomseed(os.time())
	local s0 = math.random(0x0000, 0xFFFF)
	local s1 = math.random(0x0000, 0xFFFF)
	local s2 = math.random(0x0000, 0xFFFF)
	local s3 = math.random(0x0000, 0xFFFF)
	return { s0 * 0x10000 + s1, s2 * 0x10000 + s3 }
end

local function hsv2rgb(h, s, v, a) -- [0, 1), [0, 1), [0, 1), [0, 255)
	a = a or 255
	local sector = math.floor(h * 6)
	local offset = h * 6 - sector
	local r, g, b
	if sector == 0 then
		r, g, b = 1, offset, 0
	elseif sector == 1 then
		r, g, b = 1 - offset, 1, 0
	elseif sector == 2 then
		r, g, b = 0, 1, offset
	elseif sector == 3 then
		r, g, b = 0, 1 - offset, 1
	elseif sector == 4 then
		r, g, b = offset, 0, 1
	else
		r, g, b = 1, 0, 1 - offset
	end
	r = math.floor((s * (r - 1) + 1) * 0xFF * v)
	g = math.floor((s * (g - 1) + 1) * 0xFF * v)
	b = math.floor((s * (b - 1) + 1) * 0xFF * v)
	return r, g, b, a
end

local function colour_hash(c)
	local h = fnv1a32(c .. "thecake") / 0x100000000
	local s = 0.5
	local v = 0.5 + fnv1a32(c .. "isalie") / 0x200000000
	return hsv2rgb(h, s, v)
end

local function las_func(lhs, rhs) -- locale-agnostic string sort function
	-- * Doesn't matter what this is as long as it's canonical. Built-in
	--   __lt on strings is not trustworthy because it's based on the
	--   current locale, so it's not necessarily canonical.
	for i = 1, math.max(#lhs, #rhs) do
		local lb = string.byte(lhs, i) or -math.huge
		local rb = string.byte(rhs, i) or -math.huge
		if lb ~= rb then
			return lb < rb
		end
	end
	return false
end

local ordered_pairs = user_wrap(function(tbl)
	local keys = {}
	if type(tbl) ~= "table" then
		user_error("tbl is not of type table")
	end
	for key in audited_pairs(tbl) do
		if type(key) ~= "string" and type(key) ~= "number" then
			user_error(("key %s is not a string or a number"):format(tostring(key)))
		end
		table.insert(keys, key)
	end
	table.sort(keys, function(lhs, rhs)
		local lhst = type(lhs)
		local rhst = type(rhs)
		if lhst ~= rhst then
			return las_func(lhst, rhst)
		end
		if lhs ~= rhs then
			if lhst == "number" then
				return lhs < rhs
			end
			if lhst == "string" then
				return las_func(lhs, rhs)
			end
		end
		return false
	end)
	local index = 0
	return function()
		if index < #keys then
			index = index + 1
			return keys[index], tbl[keys[index]]
		end
	end
end)

local function ilog2floor(n)
	local l = 0
	while n > 1 do
		n = bitx.rshift(n, 1)
		l = l + 1
	end
	return l
end

local function ilog2ceil(n)
	local l = ilog2floor(n)
	if bitx.lshift(1, l) < n then
		l = l + 1
	end
	return l
end

return {
	user_wrap       = user_wrap,
	user_error      = user_error,
	user_stack      = user_stack,
	user_frame_name = user_frame_name,
	call_site_name  = call_site_name,
	shared_key      = shared_key,
	reverse         = reverse,
	linked_to_array = linked_to_array,
	fnv1a32         = fnv1a32,
	crappy_seed     = crappy_seed,
	hsv2rgb         = hsv2rgb,
	colour_hash     = colour_hash,
	ordered_pairs   = ordered_pairs,
	ilog2floor      = ilog2floor,
	ilog2ceil       = ilog2ceil,
}
