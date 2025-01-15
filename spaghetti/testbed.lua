local strict = require("spaghetti.strict")
strict.wrap_env()

local spaghetti   = require("spaghetti")
local bitx        = require("spaghetti.bitx")
local plot        = require("spaghetti.plot")
local ordered_map = require("spaghetti.ordered_map")
local build       = require("spaghetti.build")

local in_tpt = rawget(_G, "tpt") and true
local audited_pairs = pairs

local function modulef(info)
	local fuzz
	local probe_length = info.probe_length or 1
	local function slot_pos(index)
		if index < 0 then
			return -3 - (-index - 1) * 2
		end
		return info.stacks * 2 + index
	end
	local stack_max_size = info.stack_max_size or 1500
	local info_inputs = info.inputs or {}
	local info_outputs = info.outputs or {}

	local function check_inputs(input_values)
		for _, input_info in ipairs(info_inputs) do
			local set_value = input_values[input_info.name]
			if not set_value then
				return nil, ("input %s test value unset"):format(input_info.name)
			end
			if input_info.never_zero then
				if set_value == 0 then
					return nil, ("input %s test value %08X does not conform to +never_zero"):format(input_info.name, set_value)
				end
			else
				if bitx.band(set_value, input_info.keepalive) ~= input_info.keepalive or
				   bitx.band(set_value, bitx.bor(input_info.keepalive, input_info.payload)) ~= set_value then
					return nil, ("input %s test value %08X does not conform to keepalive/payload %08X/%08X"):format(input_info.name, set_value, input_info.keepalive, input_info.payload)
				end
			end
		end
		return true
	end

	if info.fuzz_inputs then
		math.randomseed(os.time())
		function fuzz(fuzz_expect, ctype_at, params)
			if fuzz_expect then
				local output_values = {}
				for _, output_info in ipairs(info_outputs) do
					output_values[output_info.name] = ctype_at(slot_pos(output_info.index), 2 + probe_length)
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
							return nil, ("output %s expected to have value %08X with mask %08X"):format(output_info.name, expect_value, expect_mask)
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
			check_inputs(input_values)
			for _, input_info in ipairs(info_inputs) do
				ctype_at(slot_pos(input_info.index), -probe_length - 3, input_values[input_info.name])
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
				result = { implicit = false, values = output_values }
			end
			return result
		end
	end

	local function add_tags(named_outputs, named_inputs)
		local boundary = {}
		for _, expr in audited_pairs(named_inputs) do
			boundary[expr] = true
		end
		local initial = ordered_map.make_ordered_map()
		for _, expr in audited_pairs(named_outputs) do
			initial:add(expr)
		end
		build.hierarchy_up(initial, function(expr)
			if boundary[expr] then
				return false
			end
			if not expr.tag_ then
				expr:tag(info.tag or false)
			end
			return true
		end)
	end

	local function instantiate(named_inputs, params)
		for _, input_info in ipairs(info_inputs) do
			local input = named_inputs[input_info.name]
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

	local function design(params)
		local probes = true
		if params and params.probes ~= nil then
			probes = params.probes
		end
		local inputs = {}
		local input_initials = {}
		local outputs = {}
		local extra_parts = {}
		local named_inputs = {}
		for input_index, input_info in ipairs(info_inputs) do
			local expr = spaghetti.input(input_info.keepalive, input_info.payload)
			named_inputs[input_info.name] = expr
			if input_info.never_zero then
				expr:never_zero()
			else
				assert(bitx.band(input_info.initial, expr.keepalive_) ~= 0)
			end
			if probes then
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
		local named_outputs = instantiate(named_inputs, params)
		for output_index, output_info in ipairs(info_outputs) do
			if probes then
				-- output_info.index = output_index * 2 - 1
				table.insert(extra_parts, { type = plot.pt.LDTC, x = slot_pos(output_info.index), y = 2 })
				for i = 1, probe_length do
					table.insert(extra_parts, { type = plot.pt.FILT, x = slot_pos(output_info.index), y = 2 + i })
				end
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

	local function fuzz_outputs(input_values)
		assert(not info.fuzz_outputs_implicit, "this module does not support explicit output fuzzing")
		local ok, err = check_inputs(input_values)
		if not ok then
			return nil, err
		end
		return info.fuzz_outputs(input_values)
	end

	return {
		design       = design,
		instantiate  = instantiate,
		fuzz         = fuzz,
		fuzz_outputs = fuzz_outputs,
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
