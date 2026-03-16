local spaghetti   = require("spaghetti")
local bitx        = require("spaghetti.bitx")
local plot        = require("spaghetti.plot")
local misc        = require("spaghetti.misc")
local ordered_map = require("spaghetti.ordered_map")
local build       = require("spaghetti.build")
local user_node   = require("spaghetti.user_node")
local check       = require("spaghetti.check")

local in_tpt = rawget(_G, "tpt") and true
local audited_pairs = pairs

local function modulef(info_raw)
	local function instantiate(params, params_name)
		local info = info_raw
		if type(info) == "function" then
			info = info_raw(params, params_name)
		end

		local function check_input_value(name, value, keepalive, payload, never_zero)
			if not value then
				return nil, ("%s unset"):format(name)
			end
			if never_zero then
				if value == 0 then
					return nil, ("%s %08X does not conform to +never_zero"):format(name, value)
				end
			else
				if bitx.band(value, keepalive) ~= keepalive or
				   bitx.band(value, bitx.bor(keepalive, payload)) ~= value then
					return nil, ("%s %08X does not conform to keepalive/payload %08X/%08X"):format(name, value, keepalive, payload)
				end
			end
			return true
		end

		local fuzz
		local probe_length = info.probe_length or 1
		local function slot_pos(index)
			if index < 0 then
				return -3 - (-index - 1) * 2
			end
			return info.stacks * 2 + index
		end
		local stack_max_size = info.stack_max_size or 1500
		local info_inputs = {}
		do
			local index_unique = {}
			local name_unique = {}
			if info.inputs then
				check.table("info.inputs", info.inputs)
				info_inputs = info.inputs
				for ix_input, input_info in ipairs(info_inputs) do
					local ix_input_name = ("info.inputs[%i]"):format(ix_input)
					local name_name = ("%s.name"):format(ix_input_name)
					check.string(name_name, input_info.name)
					if name_unique[input_info.name] then
						misc.user_error("%s is not unique", name_name)
					end
					name_unique[input_info.name] = true
					local index_name = ("%s.index"):format(ix_input_name)
					check.integer_range(index_name, input_info.index, 1, 1000)
					if index_unique[input_info.index] then
						misc.user_error("%s is not unique", index_name)
					end
					index_unique[input_info.index] = true
					check.keepalive_payload(ix_input_name, input_info.keepalive, input_info.payload)
					local ok, err = check_input_value(("%s.initial"):format(ix_input_name), input_info.initial, input_info.keepalive, input_info.payload, input_info.never_zero)
					if not ok then
						misc.user_error(err)
					end
				end
			end
		end
		local info_outputs = {}
		do
			local index_unique = {}
			local name_unique = {}
			if info.outputs then
				check.table("info.outputs", info.outputs)
				info_outputs = info.outputs
				for ix_output, output_info in ipairs(info_outputs) do
					local ix_output_name = ("info.outputs[%i]"):format(ix_output)
					local name_name = ("%s.name"):format(ix_output_name)
					check.string(name_name, output_info.name)
					if name_unique[output_info.name] then
						misc.user_error("%s is not unique", name_name)
					end
					name_unique[output_info.name] = true
					local index_name = ("%s.index"):format(ix_output_name)
					check.integer_range(index_name, output_info.index, 1, 1000)
					if index_unique[output_info.index] then
						misc.user_error("%s is not unique", index_name)
					end
					index_unique[output_info.index] = true
					check.keepalive_payload(ix_output_name, output_info.keepalive, output_info.payload)
				end
			end
		end

		local function check_inputs(input_values)
			for _, input_info in ipairs(info_inputs) do
				local ok, err = check_input_value(
					("input %s test value"):format(input_info.name),
					input_values[input_info.name],
					input_info.keepalive,
					input_info.payload,
					input_info.never_zero
				)
				if not ok then
					return nil, err
				end
			end
			return true
		end

		local function get_probes(params)
			local probes = true
			if params and params.probes ~= nil then
				probes = params.probes
			end
			return probes
		end

		if info.fuzz_inputs then
			math.randomseed(os.time())
			function fuzz(fuzz_expect, ctype_at, params)
				local probes = get_probes(params)
				if fuzz_expect then
					local output_values = {}
					for _, output_info in ipairs(info_outputs) do
						output_values[output_info.name] = ctype_at(slot_pos(output_info.index), probes == true and (2 + probe_length) or 0)
					end
					if fuzz_expect.implicit then
						local ok, err = info.fuzz_outputs_implicit(fuzz_expect.values, output_values, params)
						if not ok then
							return nil, ("outputs failed implicit check: %s"):format(err)
						end
					else
						for _, output_info in ipairs(info_outputs) do
							local expect_value = fuzz_expect.values[output_info.name]
							local expect_mask = 0xFFFFFFFF
							if type(expect_value) == "table" then
								expect_mask = expect_value.mask
								expect_value = expect_value.value
							end
							if expect_value == nil then
								return nil, ("output %s expected value unset"):format(output_info.name)
							end
							if expect_value ~= false and bitx.band(bitx.bxor(output_values[output_info.name], expect_value), expect_mask) ~= 0 then
								return nil, ("output %s expected to have value %08X with mask %08X"):format(output_info.name, expect_value, expect_mask), fuzz_expect.input_values
							end
						end
					end
				end
				local input_values
				do
					local err
					input_values, err = info.fuzz_inputs(params)
					if not input_values then
						return nil, ("failed to generate inputs: %s"):format(err)
					end
				end
				do
					local ok, err = check_inputs(input_values)
					if not ok then
						return nil, err
					end
				end
				for _, input_info in ipairs(info_inputs) do
					ctype_at(slot_pos(input_info.index), probes == true and (-probe_length - 3) or 0, input_values[input_info.name])
				end
				local result
				if info.fuzz_outputs_implicit then
					result = { implicit = true, values = input_values }
				else
					local output_values
					do
						local err
						output_values, err = info.fuzz_outputs(input_values, params)
						if not output_values then
							return nil, ("failed to generate outputs: %s"):format(err)
						end
					end
					for _, output_info in ipairs(info_outputs) do
						local expect_value = output_values[output_info.name]
						if type(expect_value) == "table" then
							expect_value = expect_value.value
						end
						if expect_value then
							if output_info.never_zero then
								if expect_value == 0 then
									return nil, ("output %s expected value %08X does not conform to +never_zero"):format(output_info.name, expect_value)
								end
							else
								if bitx.band(expect_value, output_info.keepalive) ~= output_info.keepalive or
								   bitx.band(expect_value, bitx.bor(output_info.keepalive, output_info.payload)) ~= expect_value then
									return nil, ("output %s expected value %08X does not conform to keepalive/payload %08X/%08X"):format(output_info.name, expect_value, output_info.keepalive, output_info.payload)
								end
							end
						end
					end
					result = { implicit = false, values = output_values, input_values = input_values }
				end
				return result
			end
		end

		local function add_tags(named_outputs, named_inputs)
			local boundary = {}
			local input_names = {}
			local output_names = {}
			for name, expr in audited_pairs(named_inputs) do
				boundary[expr] = true
				input_names[expr] = name
			end
			local initial = ordered_map.make_ordered_map()
			for name, expr in audited_pairs(named_outputs) do
				initial:add(expr)
				output_names[expr] = name
			end
			build.hierarchy_up(initial, function(expr)
				if input_names[expr] and expr:has_default_label() then
					expr:label(input_names[expr])
				end
				if output_names[expr] and expr:has_default_label() then
					expr:label(output_names[expr])
				end
				if boundary[expr] then
					return false
				end
				if not expr.tag_ then
					expr:tag(info.tag or false)
				end
				return true
			end)
		end

		local function component(named_inputs, params)
			for _, input_info in ipairs(info_inputs) do
				local input = named_inputs[input_info.name]
				check.mt(user_node.mt_, ("input %s"):format(input_info.name), input)
				local ok, err = pcall(function()
					input:assert(input_info.keepalive, input_info.payload)
				end)
				if not ok then
					error(("input %s: %s"):format(input_info.name, err), 2)
				end
			end
			local named_outputs = info.func(named_inputs, params)
			add_tags(named_outputs, named_inputs)
			for _, output_info in ipairs(info_outputs) do
				local output = named_outputs[output_info.name]
				check.mt(user_node.mt_, ("output %s"):format(output_info.name), output)
				if not (output_info.keepalive == false and output_info.payload == false) then
					local ok, err = pcall(function()
						output:assert(output_info.keepalive, output_info.payload)
					end)
					if not ok then
						error(("output %s: %s"):format(output_info.name, err), 2)
					end
				end
			end
			return named_outputs
		end

		local function design(params, fed_value_overrides)
			local probes = get_probes(params)
			local inputs = {}
			local input_initials = {}
			local outputs = {}
			local extra_parts = {}
			local named_inputs = {}
			for input_index, input_info in ipairs(info_inputs) do
				local expr = spaghetti.input(input_info.keepalive, input_info.payload)
				named_inputs[input_info.name] = expr
				if fed_value_overrides and fed_value_overrides[input_info.name] then
					expr:feed(fed_value_overrides[input_info.name])
				end
				if input_info.never_zero then
					expr:never_zero()
				else
					assert(bitx.band(input_info.initial, expr.keepalive_) ~= 0)
				end
				if probes == true then
					-- input_info.index = input_index * 2 - 1
					table.insert(extra_parts, { type = plot.pt.FILT, x = slot_pos(input_info.index), y = -probe_length - 3, ctype = input_info.initial })
					table.insert(extra_parts, { type = plot.pt.LDTC, x = slot_pos(input_info.index), y = -probe_length - 1 })
					for i = 1, probe_length do
						table.insert(extra_parts, { type = plot.pt.FILT, x = slot_pos(input_info.index), y = -i, ctype = input_info.initial })
					end
				end
				inputs[input_info.index] = expr
				input_initials[expr] = input_info.initial
			end
			local named_outputs = component(named_inputs, params)
			for output_index, output_info in ipairs(info_outputs) do
				if probes == true then
					-- output_info.index = output_index * 2 - 1
					table.insert(extra_parts, { type = plot.pt.LDTC, x = slot_pos(output_info.index), y = 2 })
					for i = 1, probe_length do
						table.insert(extra_parts, { type = plot.pt.FILT, x = slot_pos(output_info.index), y = 2 + i })
					end
				elseif probes == "minimal" then
					table.insert(extra_parts, { type = plot.pt.LDTC, x = slot_pos(output_info.index) - 2, y = 2 })
					table.insert(extra_parts, { type = plot.pt.FILT, x = slot_pos(output_info.index) - 3, y = 3 })
				end
				outputs[output_info.index] = named_outputs[output_info.name]
			end
			for _, part in ipairs(info.extra_parts or {}) do
				table.insert(extra_parts, part)
			end
			local clobbers
			if info.clobbers then
				clobbers = {}
				for _, value in ipairs(info.clobbers) do
					clobbers[value] = true
				end
			end
			local voids
			if info.voids then
				voids = {}
				for _, value in ipairs(info.voids) do
					voids[value] = true
				end
			end
			return {
				design = spaghetti.build({
					inputs         = inputs,
					input_initials = input_initials,
					outputs        = outputs,
					clobbers       = clobbers,
					voids          = voids,
					stacks         = info.stacks,
					storage_slots  = info.storage_slots,
					work_slots     = info.work_slots,
					storage_slot_overhead_penalty = info.opt_params.storage_slot_overhead_penalty,
					work_slot_overhead_penalty    = info.opt_params.work_slot_overhead_penalty,
					stack_max_size = stack_max_size,
				}),
				extra_parts = extra_parts,
				opt_params  = info.opt_params,
			}
		end

		local function fuzz_outputs(input_values, params)
			assert(not info.fuzz_outputs_implicit, "this module does not support explicit output fuzzing")
			local ok, err = check_inputs(input_values)
			if not ok then
				return nil, err
			end
			return info.fuzz_outputs(input_values, params)
		end

		local function fuzz_outputs_implicit(input_values, output_values, params)
			assert(info.fuzz_outputs_implicit, "this module does not support implicit output fuzzing")
			local ok, err = check_inputs(input_values)
			if not ok then
				return nil, err
			end
			return info.fuzz_outputs_implicit(input_values, output_values, params)
		end

		return {
			design                = design,
			component             = component,
			fuzz                  = fuzz,
			fuzz_outputs          = fuzz_outputs,
			fuzz_outputs_implicit = fuzz_outputs_implicit,
		}
	end

	return {
		instantiate = instantiate,
	}
end

local function any()
	local v = bitx.bor(math.random(0x0000, 0xFFFF), bitx.lshift(math.random(0x0000, 0xFFFF), 16))
	return v == 0 and 0x1F or v
end

return {
	module = modulef,
	any    = any,
}
