local params = ...

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

local function pack(...)
	return { ... }, select("#", ...)
end

local function require_overlay(...)
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
	local pc, pcn = pack(pcall(...))
	rawset(_G, "require", old_require)
	for key in pairs(to_kill) do
		package.loaded[key] = nil
	end
	package.path = old_ppath
	package.cpath = old_pcpath
	assert(unpack(pc, 1, pcn))
end

require_overlay(function()
	sim.clearSim()
	if params.spaghetti_install_path then
		package.path = params.spaghetti_install_path .. "/?.lua" .. ";" .. package.path
		package.path = params.spaghetti_install_path .. "/?/init.lua" .. ";" .. package.path
		package.cpath = params.spaghetti_install_path .. "/?.so" .. ";" .. package.cpath
	end
	local plot = require("spaghetti.plot")
	local optimize = require("spaghetti.optimize")
	local design, extra_parts = dofile(params.design_path)
	local optimizer = optimize.make_optimizer(1337)
	local temp_initial = 1
	local temp_final = 0.95
	optimizer:state(design:initial(), temp_initial)
	optimizer:dispatch(temp_final, 1e-7, 1000)
	local text_x, text_y = 80, 120
	local box_size = 5
	local cancel = Button:new(text_x, text_y + 15, 80, 15, "Cancel")
	local done = false
	local function tick()
		local result, temperature = optimizer:state()
		local energy_linear, storage_used, parts, slot_states = result:energy()
		local function box_at(x, y, c)
			local func, r, g, b = gfx.drawRect, 128, 128, 128
			if c then
				func, r, g, b = gfx.fillRect, hsv2rgb(((c * 123764.128376) % 72563) / 72563, 0.5, 1)
			end
			func(text_x + x * (box_size + 1), text_y + 35 + y * (box_size + 1), box_size, box_size, r, g, b, 255)
		end
		for i = 1, #slot_states.storage_slot_states do
			local states = slot_states.storage_slot_states[i]
			for j = 1, slot_states.storage_slots do
				box_at(j + slot_states.work_slots + 1, i, states[j])
			end
		end
		for i = 1, #slot_states.work_slot_states do
			local states = slot_states.work_slot_states[i]
			for j = 1, slot_states.work_slots do
				box_at(slot_states.work_slots - j + 1, i, states[j])
			end
		end
		local progress = (temperature - temp_initial) / (temp_final - temp_initial)
		if not done and optimizer:ready() then
			done = true
			plot.plot(100, 100, result, extra_parts)
			tpt.set_pause(0)
			cancel:text("OK")
		end
		if done then
			gfx.drawText(text_x, text_y, "Done")
		else
			gfx.drawText(text_x, text_y, ("Optimizing; about %i%% done; parts: %i; storage used: %i; energy: %.2f"):format(math.floor(progress * 100), parts, storage_used, energy_linear))
		end
	end
	cancel:action(function()
		optimizer:cancel()
		interface.removeComponent(cancel)
		event.unregister(event.tick, tick)
	end)
	interface.addComponent(cancel)
	event.register(event.tick, tick)
end)
