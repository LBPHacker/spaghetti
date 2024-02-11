local audited_pairs = pairs

local function require_overlay(func)
	local patched = {}
	local function add_path(path)
		if not patched[path] then
			patched[path] = true
			package.path = path .. "/?.lua" .. ";" .. package.path
			package.path = path .. "/?/init.lua" .. ";" .. package.path
			package.cpath = path .. "/?.so" .. ";" .. package.cpath
		end
	end
	local function add_path_from_func(func, level)
		local script_path = debug.getinfo(func).source
		assert(script_path:sub(1, 1) == "@", "something is fishy")
		script_path = script_path:sub(2)
		local slash_at = script_path:match("()[\\/][^\\/]+$")
		local path
		if slash_at then
			path = script_path:sub(1, slash_at - 1)
		else
			path = "."
		end
		for i = 1, level or 0 do
			path = path .. "/.."
		end
		add_path(path)
	end
	local old_require = rawget(_G, "require")
	local old_ppath = package.path
	local old_pcpath = package.cpath
	local to_kill = {}
	rawset(_G, "require", function(modname)
		local loaded = not package.loaded[modname]
		local mod = old_require(modname)
		if loaded and package.loaded[modname] then
			to_kill[modname] = true
		end
		return mod
	end)
	xpcall(function()
		func(add_path, add_path_from_func)
	end, function(err)
		print(err)
		print(debug.traceback())
	end)
	rawset(_G, "require", old_require)
	for key in audited_pairs(to_kill) do
		package.loaded[key] = nil
	end
	package.path = old_ppath
	package.cpath = old_pcpath
end

local project_path, func = ...
require_overlay(function(add_path, add_path_from_func)
	add_path_from_func(1, 1)
	add_path(project_path)
	func()
end)
