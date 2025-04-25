local optimize   = _G.require("spaghetti.optimize")
local plan       = require("spaghetti.plan")
local misc       = require("spaghetti.misc")
local modulepack = require("modulepack")

local in_tpt = rawget(_G, "tpt") and true
local audited_pairs = pairs

local function run_internal(params)
	local module_instance = params.module.instantiate(params.module_params)
	local design_params = params.design_params
	local info = module_instance.design(design_params)

	local seed                = info.opt_params and info.opt_params.seed                or misc.crappy_seed()
	local thread_count        = info.opt_params and info.opt_params.thread_count        or 4
	local round_length        = info.opt_params and info.opt_params.round_length        or 1000
	local rounds_per_exchange = info.opt_params and info.opt_params.rounds_per_exchange or 0
	local text_x              = info.text_x     or 80
	local text_y              = info.text_y     or 120
	local plot_x              = info.plot_x     or 100
	local plot_y              = info.plot_y     or 100
	local fuzz = true
	if params.fuzz ~= nil then
		fuzz = params.fuzz
	end
	local pause = false
	if params.pause ~= nil then
		pause = params.pause
	end
	local vt100 = false
	if params.vt100 ~= nil then
		vt100 = params.vt100
	end

	local schedule
	if info.opt_params and info.opt_params.schedule then
		schedule = optimize.make_schedule({
			durations    = info.opt_params.schedule.durations,
			temperatures = info.opt_params.schedule.temperatures,
		})
	else
		local temp_initial = info.opt_params and info.opt_params.temp_initial or 1
		local temp_final   = info.opt_params and info.opt_params.temp_final   or 0.9995
		local temp_loss    = info.opt_params and info.opt_params.temp_loss    or 1e-9
		local duration = math.floor((temp_initial - temp_final) / temp_loss)
		schedule = optimize.make_schedule({
			durations    = { duration },
			temperatures = { temp_initial, temp_final },
		})
	end

	local output_handle = io.stdout
	local open_handle
	if params.output then
		open_handle = assert(io.open(params.output, "w"))
		output_handle = open_handle
	end
	local function exit(code)
		if open_handle then
			assert(open_handle:close())
		end
		os.exit(code or 0)
	end

	local output_view = "none"
	if params.output_view ~= nil then
		output_view = params.output_view
	end

	local output_type = "plot"
	if params.output_type ~= nil then
		output_type = params.output_type
	end
	if output_type == "graph" then
		info.design.debug_info:dump_graph(output_handle, false)
		return
	end
	if output_type == "graph_with_constants" then
		info.design.debug_info:dump_graph(output_handle, true)
		return
	end
	if output_type == "stats" then
		info.design.debug_info:dump_stats(output_handle)
		return
	end

	local optimizer = optimize.make_optimizer(seed[1], seed[2], thread_count)
	if in_tpt then
		sim.clearSim()
	end
	optimizer:state(info.design.design:initial(), schedule, 0)
	optimizer:dispatch(round_length, rounds_per_exchange)

	if not in_tpt then
		rawset(_G, "print", function(msg)
			io.stderr:write(msg .. "\n")
		end)
	end

	local print_func
	if in_tpt then
		print_func = gfx.drawText
	else
		print_func = function(_, _, msg)
			print(msg)
		end
	end

	local box_size = 5
	local box_buf
	local function box_reset()
		if not in_tpt then
			box_buf = {}
		end
	end
	local function drawCrossedRect(x, y, w, h, r, g, b)
		gfx.drawRect(x, y, w, h, r, g, b)
		gfx.drawLine(x + 2, y + 2, x + 2, y + 2, r, g, b)
	end
	local function vt100_source_char(c)
		local chr, color
		if c then
			chr = string.char(33 + math.floor(misc.fnv1a32(c .. "thecake") / 0x100000000 * 94))
			color = "\27[1;3" .. (math.floor(misc.fnv1a32(c .. "isalie") / 0x100000000 * 7) + 1) .. "m"
		elseif c == false then
			chr = "x"
			color = "\27[0m"
		else
			chr = "."
			color = "\27[0m"
		end
		return (vt100 and color or "") .. chr
	end
	local function box_at(x, y, c)
		if in_tpt then
			local func, r, g, b = gfx.drawRect, 128, 128, 128
			if c then
				func = gfx.fillRect
				r, g, b = misc.colour_hash(c)
			elseif c == false then
				func = drawCrossedRect
			end
			func(text_x + x * (box_size + 1), text_y + 47 + y * (box_size + 1), box_size, box_size, r, g, b, 255)
		else
			if not box_buf[y] then
				box_buf[y] = {}
			end
			local row = box_buf[y]
			while #row < x do
				table.insert(row, " ")
			end
			row[x] = vt100_source_char(c)
		end
	end
	local function box_print()
		if not in_tpt then
			print("")
			for y = 1, #box_buf do
				print("  " .. table.concat(box_buf[y]))
			end
			if vt100 then
				print("\27[0m")
			else
				print("")
			end
		end
	end

	local cancel
	local optimizer_plan
	local use_current
	if in_tpt then
		cancel = ui.button(text_x, text_y + 27, 80, 15, "Cancel")
		use_current = ui.button(text_x + 90, text_y + 27, 80, 15, "Use current")
	end
	local runner_state = "optimizing"

	local function current_state()
		local state, schedule, progress, temperature = optimizer:state()
		local energy_linear, storage_used, parts, slot_states, voids = state:energy()
		local progress = progress / schedule:duration()
		return {
			state         = state,
			progress      = progress,
			temperature   = temperature,
			energy_linear = energy_linear,
			storage_used  = storage_used,
			parts         = parts,
			slot_states   = slot_states,
			voids         = voids,
		}
	end

	local aftersim
	local tick
	local defer_done = false
	local function done()
		if in_tpt then
			cancel:text("OK")
			sim.paused(pause)
		end
		runner_state = "done"
	end

	local failed, final_state
	local function stop_optimizing(discard)
		if runner_state == "optimizing" then
			final_state = current_state()
			optimizer:cancel()
			if not discard then
				if in_tpt then
					interface.removeComponent(use_current)
				end
				local err
				optimizer_plan, err = final_state.state:plan()
				if optimizer_plan then
					final_state.stacks_used = optimizer_plan.stacks_used
					if in_tpt then
						plan.make_plan(plot_x, plot_y, optimizer_plan, info.extra_parts, {}, params.debug)
					end
					if fuzz and module_instance.fuzz and in_tpt then
						runner_state = "fuzzing"
					end
				else
					failed = err
				end
			end
		end
		if runner_state == "optimizing" then
			done()
		end
	end

	local function get_source_tag(source)
		local expr = info.design.debug_info.source_index_to_expr[source]
		local tag = expr.type_
		if expr.tag_ then
			tag = tostring(expr.tag_)
		end
		return misc.fnv1a32(tag), tag
	end

	local fuzzing_iterations = 0
	local fuzzing_failed = false
	local function draw_state(state)
		box_reset()
		local voids = {}
		for _, index in ipairs(state.voids) do
			voids[index] = true
		end
		local slot_states = state.slot_states
		for i = 1, #slot_states.storage_slot_states do
			local states = slot_states.storage_slot_states[i]
			for j = 1, slot_states.storage_slots do
				local source = states[j]
				if voids[j] then
					source = false
				end
				if source then
					if output_view == "tag" then
						source = get_source_tag(source)
					end
				end
				box_at(j + slot_states.work_slots + 1, i, source)
			end
		end
		for i = 1, #slot_states.work_slot_states do
			local states = slot_states.work_slot_states[i]
			for j = 1, slot_states.work_slots do
				local source = states[j]
				if source then
					if output_view == "tag" then
						source = get_source_tag(source)
					end
				end
				box_at(slot_states.work_slots - j + 1, i, source)
			end
		end
		local str = {}
		if runner_state == "optimizing" then
			table.insert(str, ("Optimizing; temperature: %f, about %i%% done"):format(state.temperature, math.floor(state.progress * 100)))
		else
			table.insert(str, "Done")
			if not failed then
				table.insert(str, ("; stacks used: %i"):format(state.stacks_used))
			end
		end
		table.insert(str, ("; parts: %i; work slots used: %i; storage slots used: %i; energy: %.2f"):format(state.parts, state.slot_states.work_slots, state.storage_used, state.energy_linear))
		if runner_state ~= "optimizing" then
			if failed then
				table.insert(str, ("\nPlanning failed: %s"):format(failed))
			end
			if fuzzing_failed then
				table.insert(str, ("\nFuzzing failed: %s"):format(tostring(fuzzing_failed)))
			end
			if runner_state == "fuzzing" then
				table.insert(str, ("\nFuzzing; %i iterations done"):format(fuzzing_iterations))
			end
		end
		box_print()
		print_func(text_x, text_y, table.concat(str))
	end

	local fuzz_expect
	local function ctype_at(x, y, ...)
		local id = sim.partID(x + plot_x, y + plot_y)
		local value = sim.partProperty(id, "ctype", ...)
		if value then
			value = value % 0x100000000
		end
		return value
	end
	aftersim = modulepack.xpcall_wrap(function()
		if runner_state == "fuzzing" then
			if not event.AFTERSIM and tpt.drawCap() ~= 0 then
				fuzzing_failed = "event.aftersim is not available and draw cap isn't 0"
				defer_done = true
			else
				local failed_obj
				fuzz_expect, failed_obj = module_instance.fuzz(fuzz_expect, ctype_at, design_params)
				if not fuzz_expect then
					fuzzing_failed = failed_obj or "?"
					defer_done = true
				end
				fuzzing_iterations = fuzzing_iterations + 1
			end
		end
	end)
	tick = modulepack.xpcall_wrap(function()
		if defer_done then
			done()
			defer_done = false
		end
		if runner_state == "optimizing" and optimizer:ready() then
			stop_optimizing()
		end
		local state = final_state
		if runner_state == "optimizing" then
			state = current_state()
		end
		if state then
			draw_state(state)
		end
		if in_tpt and not event.AFTERSIM then
			aftersim()
		end
	end)

	if in_tpt then
		cancel:action(function()
			stop_optimizing(true)
			done()
			event.unregister(event.TICK, tick)
			event.unregister(event.AFTERSIM or event.TICK, aftersim)
			interface.removeComponent(use_current)
			interface.removeComponent(cancel)
		end)
		use_current:action(function()
			stop_optimizing()
		end)
		interface.addComponent(use_current)
		interface.addComponent(cancel)
		event.register(event.TICK, tick)
		event.register(event.AFTERSIM or event.TICK, aftersim)
	else
		while true do
			tick()
			if runner_state == "done" then
				break
			end
			optimize.sleep(1)
		end
		print(("Seed: 0x%08X 0x%08X\n"):format(seed[1], seed[2]))
		if output_view == "tag" then
			local seen = {}
			local ordered = {}
			for source in audited_pairs(info.design.debug_info.source_index_to_expr) do
				local c, tag = get_source_tag(source)
				if not seen[tag] then
					seen[tag] = true
					table.insert(ordered, { tag = tag, c = c })
				end
			end
			table.sort(ordered, function(lhs, rhs)
				return lhs.tag < rhs.tag
			end)
			assert(output_handle:write("Tag legend:\n"))
			for _, item in ipairs(ordered) do
				assert(output_handle:write(("  %s \27[0m%s\n"):format(vt100_source_char(item.c), item.tag)))
			end
		end
		if output_type == "nothing" then
			-- nothing
		elseif output_type == "work" then
			info.design.debug_info:dump_work(output_handle, final_state.slot_states)
		else
			if optimizer_plan then
				assert(output_handle:write(plan.serialize_plan(optimizer_plan, info.extra_parts, seed)))
			end
		end
		exit()
	end
end

return {
	run_internal = run_internal,
}
