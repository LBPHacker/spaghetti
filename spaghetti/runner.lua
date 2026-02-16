local plan       = require("spaghetti.plan")
local misc       = require("spaghetti.misc")
local check      = require("spaghetti.check")
local modulepack = require("modulepack")

local in_tpt = rawget(_G, "tpt") and true
local audited_pairs = pairs

local function run_internal(params, params_name)
	if rawget(_G, "spaghetti_run") then
		spaghetti_run.cancel()
	end

	params_name = params_name or "params"
	local module_instance = params.module.instantiate(params.module_params, params_name .. ".module_params")
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

	if output_type ~= "plot" and output_type ~= "work" then
		misc.user_error("unknown output type %s", output_type)
	end
	if in_tpt then
		sim.clearSim()
	end

	local print_func
	if in_tpt then
		print_func = gfx.drawText
	else
		print_func = function(_, _, msg)
			print(msg)
		end
	end

	local optimizer_plan, final_optimizer_state

	local draw_state
	do
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
				func(text_x + x * (box_size + 1), text_y + 59 + y * (box_size + 1), box_size, box_size, r, g, b, 255)
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

		function draw_state(state)
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
			box_print()
		end
	end

	local function describe_state(str, state)
		table.insert(str, ("\nParts: %i; work slots used: %i; storage slots used: %i; energy: %.2f"):format(state.parts, state.slot_states.work_slots, state.storage_used, state.energy_linear))
	end

	local running_outside_tpt
	if not in_tpt then
		running_outside_tpt = true
	end
	local current_task = 0
	local tasks = {}

	if #info.design.debug_info.occ_problems > 0 then
		local occsat = _G.require("spaghetti.occsat")
		local occ_problems = info.design.debug_info.occ_problems
		for ix_problem, problem in ipairs(occ_problems) do
			local occsat_solver = occsat.make_solver(params.occ_solver, thread_count)
			occsat_solver:problem(occsat.make_problem(problem.nvars, problem.clauses))
			local failed
			local ready = false
			table.insert(tasks, {
				start = function()
					occsat_solver:dispatch()
				end,
				cancel = function()
					occsat_solver:cancel()
				end,
				ready = function()
					return ready
				end,
				tick = function()
					local str = {}
					if occsat_solver:ready() then
						local result, satisfiable = occsat_solver:solution()
						if result == "satisfiable" then
							local expr_values = problem:interpret_solution(satisfiable)
							local config_strs = {}
							for expr, value in audited_pairs(expr_values) do
								table.insert(config_strs, (" - %s: %08X"):format(tostring(expr), value))
							end
							failed = ("invalid configuration found for offline correctness check domain %s:\n%s\n"):format(domain, table.concat(config_strs, "\n"))
						else
							ready = true
						end
					end
					if failed then
						table.insert(str, ("Done\nVerification of OCC domain %i/%i on top of leaf %s failed: %s"):format(ix_problem, #occ_problems, tostring(problem.leaf, failed)))
					else
						table.insert(str, ("Verifying OCC domain %i/%i on top of leaf %s"):format(ix_problem, #occ_problems, tostring(problem.leaf)))
					end
					print_func(text_x, text_y, table.concat(str))
					if not in_tpt and failed then
						running_outside_tpt = false
					end
				end,
				tick_period = 0.01,
			})
		end
	end

	do
		local schedule
		local optimize = _G.require("spaghetti.optimize")
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
		local optimizer = optimize.make_optimizer(seed[1], seed[2], thread_count)
		local design = optimize.make_design(info.design.design_params)
		optimizer:state(design:initial(), schedule, 0)

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

		local use_current_button
		local failed
		local ready = false
		local optimizing = true
		local function stop_optimizing(discard)
			final_optimizer_state = current_state()
			optimizer:cancel()
			if not discard then
				if in_tpt then
					interface.removeComponent(use_current_button)
				end
				local err
				optimizer_plan, err = final_optimizer_state.state:plan()
				if optimizer_plan then
					final_optimizer_state.stacks_used = optimizer_plan.stacks_used
					if in_tpt then
						plan.make_plan(plot_x, plot_y, optimizer_plan, info.extra_parts, {}, params.debug)
					end
					ready = true
				else
					failed = err
				end
			end
			optimizing = false
		end

		table.insert(tasks, {
			start = function()
				if in_tpt then
					use_current_button = ui.button(text_x + 90, text_y + 39, 80, 15, "Use current")
					use_current_button:action(function()
						stop_optimizing()
					end)
					interface.addComponent(use_current_button)
				end
				optimizer:dispatch(round_length, rounds_per_exchange)
			end,
			cancel = function()
				if optimizing then
					stop_optimizing(true)
				end
			end,
			cleanup = function()
				if in_tpt then
					interface.removeComponent(use_current_button)
				end
			end,
			ready = function()
				return ready
			end,
			tick = function()
				local state = current_state()
				draw_state(state)
				local str = {}
				if optimizing and optimizer:ready() then
					stop_optimizing()
				end
				if failed then
					table.insert(str, ("Done\nPlanning failed: %s"):format(failed))
				else
					table.insert(str, ("Optimizing; temperature: %f, about %i%% done"):format(state.temperature, math.floor(state.progress * 100)))
				end
				describe_state(str, state)
				print_func(text_x, text_y, table.concat(str))
				if not in_tpt and failed then
					running_outside_tpt = false
				end
			end,
		})
	end

	if fuzz and module_instance.fuzz and in_tpt then
		local function ctype_at(x, y, ...)
			local id = sim.partID(x + plot_x, y + plot_y)
			local value = sim.partProperty(id, "ctype", ...)
			if value then
				value = value % 0x100000000
			end
			return value
		end

		local fuzzing_iterations = 0
		local fuzzing_failed = false
		local fuzzing_input_values
		local fuzz_expect
		local explain_button, explain_button_added
		table.insert(tasks, {
			ready = function()
				return false
			end,
			aftersim = function()
				if not fuzzing_failed then
					local failed_obj
					fuzz_expect, failed_obj, fuzzing_input_values = module_instance.fuzz(fuzz_expect, ctype_at, design_params)
					if not fuzz_expect then
						fuzzing_failed = failed_obj or "?"
					end
					fuzzing_iterations = fuzzing_iterations + 1
				end
			end,
			tick = function()
				draw_state(final_optimizer_state)
				local str = {}
				table.insert(str, ("Done; stacks used: %i"):format(final_optimizer_state.stacks_used))
				describe_state(str, final_optimizer_state)
				if fuzzing_failed then
					table.insert(str, ("\nFuzzing failed: %s"):format(tostring(fuzzing_failed)))
				else
					table.insert(str, ("\nFuzzing; %i iterations done"):format(fuzzing_iterations))
				end
				print_func(text_x, text_y, table.concat(str))
				if in_tpt and fuzzing_input_values and not explain_button_added then
					explain_button = ui.button(text_x + 90, text_y + 39, 80, 15, "Explain")
					explain_button:action(function()
						local dump_info = module_instance.design(design_params, fuzzing_input_values)
						dump_info.design.debug_info:dump_graph(output_handle, false)
						if open_handle then
							assert(open_handle:close())
							open_handle = nil
						end
						interface.removeComponent(explain_button)
						explain_button = nil
					end)
					interface.addComponent(explain_button)
					explain_button_added = true
				end
			end,
			cleanup = function()
				if in_tpt and explain_button then
					interface.removeComponent(explain_button)
				end
			end,
		})
	end

	if not in_tpt then
		rawset(_G, "print", function(msg)
			io.stderr:write(msg .. "\n")
		end)
	end

	local cancel_button
	if in_tpt then
		cancel_button = ui.button(text_x, text_y + 39, 80, 15, "Cancel")
	end

	local aftersim
	local tick

	local function get_source_tag(source)
		local expr = info.design.debug_info.source_index_to_expr[source]
		local tag = expr.type_
		if expr.tag_ then
			tag = tostring(expr.tag_)
		end
		return misc.fnv1a32(tag), tag
	end

	aftersim = modulepack.xpcall_wrap(function()
		if tasks[current_task] and tasks[current_task].aftersim then
			tasks[current_task].aftersim()
		end
	end)
	local unregister
	tick = modulepack.xpcall_wrap(function()
		while true do
			if not tasks[current_task] then
				if current_task >= #tasks then
					if unregister then
						unregister()
					end
					return
				end
				current_task = current_task + 1
				if tasks[current_task].start then
					tasks[current_task].start()
				end
			end
			if tasks[current_task].tick then
				tasks[current_task].tick()
			end
			if not tasks[current_task].ready() then
				break
			end
			if tasks[current_task].cleanup then
				tasks[current_task].cleanup()
			end
			tasks[current_task] = false
		end
	end)

	if in_tpt then
		local function cancel()
			if tasks[current_task] then
				if tasks[current_task].cancel then
					tasks[current_task].cancel()
				end
				if tasks[current_task].cleanup then
					tasks[current_task].cleanup()
				end
			end
			unregister()
		end
		local spaghetti_run = {
			cancel = cancel,
		}
		rawset(_G, "spaghetti_run", spaghetti_run)
		cancel_button:action(cancel)
		interface.addComponent(cancel_button)
		function unregister()
			event.unregister(event.TICK, tick)
			event.unregister(event.AFTERSIM, aftersim)
			interface.removeComponent(cancel_button)
			sim.paused(pause)
		end
		event.register(event.TICK, tick)
		event.register(event.AFTERSIM, aftersim)
	else
		local optimize = _G.require("spaghetti.optimize")
		while true do
			tick()
			if not tasks[current_task] then
				break
			end
			if not running_outside_tpt then
				break
			end
			optimize.sleep(tasks[current_task] and tasks[current_task].tick_period or 1)
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
			info.design.debug_info:dump_work(output_handle, final_optimizer_state.slot_states)
		else
			if optimizer_plan then
				assert(output_handle:write(plan.serialize_plan(optimizer_plan, info.extra_parts, seed)))
			end
		end
		exit()
	end
end

return {
	run_internal = misc.user_wrap(run_internal),
}
