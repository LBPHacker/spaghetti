local forward_frame_name = "=[spaghetti_forward_frame]"
local user_wrap = loadstring([[
	local function packn(...)
		return { select("#", ...), ... }
	end
	local params_in = packn(...)
	local params_out = packn(params_in[2](unpack(params_in, 3, params_in[1] + 1)))
	return unpack(params_out, 2, params_out[1] + 1)
]], forward_frame_name)

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

local function user_frame_name()
	local _, err = pcall(user_error, "@")
	return assert(err:match("^(.*): @$"))
end

local function values_to_keys(tbl)
	local keys = {}
	for _, value in pairs(tbl) do
		keys[value] = true
	end
	return keys
end

local function count_keys(tbl)
	local count = 0
	for _ in pairs(tbl) do
		count = count + 1
	end
	return count
end

local function keys_to_array(tbl)
	local arr = {}
	for key in pairs(tbl) do
		table.insert(arr, key)
	end
	return arr
end

local function shared_key(one, other)
	for key in pairs(one) do
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

local function shallow_copy(tbl)
	local res = {}
	for key, value in pairs(tbl) do
		res[key] = value
	end
	return res
end

local function get_by_keys(tbl, keys)
	local res = {}
	for key in pairs(keys) do
		res[key] = tbl[key]
	end
	return res
end

return {
	user_wrap       = user_wrap,
	user_error      = user_error,
	user_frame_name = user_frame_name,
	values_to_keys  = values_to_keys,
	count_keys      = count_keys,
	keys_to_array   = keys_to_array,
	shared_key      = shared_key,
	reverse         = reverse,
	linked_to_array = linked_to_array,
	shallow_copy    = shallow_copy,
	get_by_keys     = get_by_keys,
}
