local strict      = require("spaghetti.strict")
local check       = require("spaghetti.check")
local user_node   = require("spaghetti.user_node")
local graph       = require("spaghetti.graph")
local misc        = require("spaghetti.misc")
local id_store    = require("spaghetti.id_store")
local bitx        = require("spaghetti.bitx")
local plot        = require("spaghetti.plot")
local ordered_map = require("spaghetti.ordered_map")

local audited_pairs = pairs

local occ_problem_m, occ_problem_i = strict.make_mt("spaghetti.build.occ_problem")

function occ_problem_i:interpret_solution(satisfiable)
	local expr_values = {}
	local config = {}
	for _, index in ipairs(satisfiable) do
		config[math.abs(index)] = index > 0
	end
	for expr, bits in audited_pairs(expr_bits) do
		if expr.marked_occ_root_[domain] or expr == leaf then
			local value = 0
			for i = 0, 31 do
				local config_value = config[bits[i]]
				assert(config_value ~= nil)
				if config_value then
					value = bitx.bor(value, bitx.lshift(1, i))
				end
			end
			expr_values[expr] = value
		end
	end
	return expr_values
end

local debug_info_m, debug_info_i = strict.make_mt("spaghetti.build.debug_info")

local function hierarchy_up(output_keys, visit)
	graph.bfs(output_keys, function(expr)
		local neighbours = ordered_map.make_ordered_map()
		if visit(expr) then
			if expr.type_ == "composite" then
				for index, name in ipairs(expr.info_.params) do
					local param = expr.params_[name]
					neighbours:add(param.node)
				end
			end
		end
		return neighbours
	end)
end

local function ts_down(output_keys, visit)
	local children = {}
	local parents = {}
	local initial = ordered_map.make_ordered_map()
	hierarchy_up(output_keys, function(expr)
		children[expr] = ordered_map.make_ordered_map()
		parents[expr] = ordered_map.make_ordered_map()
		return true
	end)
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "composite" then
			for index, name in ipairs(expr.info_.params) do
				local parent = expr.params_[name]
				children[parent.node]:add(expr)
				parents[expr]:add(parent.node)
			end
		end
		if expr.type_ == "input" or expr.type_ == "constant" then
			initial:add(expr)
		end
		return true
	end)
	graph.ts(initial, children, parents, function(expr)
		visit(expr)
		return true
	end)
end

local function check_zeroness(output_keys)
	hierarchy_up(output_keys, function(expr)
		if not expr.marked_zeroable_ and expr:can_be_zero() then
			misc.user_error("%s can be zero despite not being marked zeroable", tostring(expr))
		end
		if expr.marked_zeroable_ and not expr:can_be_zero_indirectly() then
			misc.user_error("%s cannot be zero even indirectly despite being marked zeroable", tostring(expr))
		end
		if expr.type_ == "composite" then
			expr:check_inputs_()
		end
		if output_keys:get(expr) then
			if expr.marked_zeroable_ then
				misc.user_error("output %s is marked zeroable", tostring(expr))
			end
		end
		return true
	end)
end

local function check_connectivity(output_keys, inputs)
	local seen = {}
	local expect_input = {}
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "input" then
			expect_input[expr] = true
		end
		seen[expr] = true
		return true
	end)
	local seen_input = {}
	for key, input in audited_pairs(inputs) do
		seen_input[input] = true
		if not seen[input] then
			local valuename = "info.inputs[" .. key .. "]"
			misc.user_error("%s is not connected", valuename)
		end
	end
	for expr in audited_pairs(expect_input) do
		if not seen_input[expr] then
			misc.user_error("%s is not listed as an input", tostring(expr))
		end
	end
end

local function check_info(info)
	check.table("info", info)
	check.integer("info.stacks", info.stacks)
	if info.stacks < 1 then
		misc.user_error("info.stacks is out of bounds")
	end
	check.integer("info.storage_slots", info.storage_slots)
	if info.storage_slots < 1 then
		misc.user_error("info.storage_slots is out of bounds")
	end
	check.integer("info.work_slots", info.work_slots)
	if info.work_slots < 2 then
		misc.user_error("info.work_slots is out of bounds")
	end
	local storage_slot_overhead_penalty = info.storage_slot_overhead_penalty
	if storage_slot_overhead_penalty == nil then
		storage_slot_overhead_penalty = 10
	end
	check.integer("info.storage_slot_overhead_penalty", storage_slot_overhead_penalty)
	if storage_slot_overhead_penalty < 0 then
		misc.user_error("info.storage_slot_overhead_penalty is out of bounds")
	end
	local work_slot_overhead_penalty = info.work_slot_overhead_penalty
	if work_slot_overhead_penalty == nil then
		work_slot_overhead_penalty = 10
	end
	check.integer("info.work_slot_overhead_penalty", work_slot_overhead_penalty)
	if work_slot_overhead_penalty < 0 then
		misc.user_error("info.work_slot_overhead_penalty is out of bounds")
	end
	check.integer("info.stack_max_size", info.stack_max_size)
	if info.stack_max_size < 10 or info.stack_max_size > 1500 then
		misc.user_error("info.stack_max_size is out of bounds")
	end
	if info.on_progress ~= nil then
		check.func("info.on_progress", info.on_progress)
	end
	local input_initials
	if info.input_initials ~= nil then
		check.table("info.input_initials", info.input_initials)
		input_initials = info.input_initials
	else
		input_initials = {}
	end
	check.table("info.inputs", info.inputs)
	for key, value in audited_pairs(info.inputs) do
		local keyname = "info.inputs key " .. tostring(key)
		check.integer(keyname, key)
		if key < 1 or key > info.storage_slots then
			misc.user_error("%s is out of bounds", keyname)
		end
		local valuename = "info.inputs[" .. key .. "]"
		check.mt(user_node.mt_, valuename, value)
		if value.type_ ~= "input" then
			misc.user_error("%s is not an input", valuename)
		end
	end
	check.table("info.outputs", info.outputs)
	for key, value in audited_pairs(info.outputs) do
		local keyname = "info.outputs key " .. tostring(key)
		check.integer(keyname, key)
		if key < 0 then
			local offset = -key
			if offset < 1 or offset > info.work_slots then
				misc.user_error("%s is out of bounds", keyname)
			end
		else
			if key < 1 or key > info.storage_slots then
				misc.user_error("%s is out of bounds", keyname)
			end
		end
		local valuename = "info.outputs[" .. key .. "]"
		check.mt(user_node.mt_, valuename, value)
		if value.marked_zeroable_ then
			misc.user_error("%s is marked zeroable", valuename)
		end
	end
	local clobbers = {}
	if info.clobbers ~= nil then
		clobbers = info.clobbers
		check.table("info.clobbers", info.clobbers)
		for key, value in audited_pairs(info.clobbers) do
			local keyname = "info.clobbers key " .. tostring(key)
			check.integer(keyname, key)
			if key < 0 or key > info.storage_slots then
				misc.user_error("%s is out of bounds", keyname)
			end
			local valuename = "info.clobbers[" .. key .. "]"
			if value ~= true then
				misc.user_error("%s is not true", valuename)
			end
		end
		local shared_key = misc.shared_key(info.inputs, info.clobbers)
		if shared_key then
			misc.user_error("input.inputs and info.clobbers share key %s", tostring(shared_key))
		end
	end
	local voids = {}
	if info.voids ~= nil then
		voids = info.voids
		check.table("info.voids", info.voids)
		for key, value in audited_pairs(info.voids) do
			local keyname = "info.voids key " .. tostring(key)
			check.integer(keyname, key)
			if key < 0 or key > info.storage_slots then
				misc.user_error("%s is out of bounds", keyname)
			end
			local valuename = "info.voids[" .. key .. "]"
			if value ~= true then
				misc.user_error("%s is not true", valuename)
			end
		end
		local shared_key = misc.shared_key(info.inputs, info.voids)
		if shared_key then
			misc.user_error("input.inputs and info.voids share key %s", tostring(shared_key))
		end
		local shared_key = misc.shared_key(clobbers, info.voids)
		if shared_key then
			misc.user_error("input.clobbers and info.voids share key %s", tostring(shared_key))
		end
		local shared_key = misc.shared_key(info.outputs, info.voids)
		if shared_key then
			misc.user_error("input.outputs and info.voids share key %s", tostring(shared_key))
		end
	end
	local output_slots = ordered_map.make_ordered_map()
	local output_keys = ordered_map.make_ordered_map()
	do
		local keys = {}
		for key in misc.ordered_pairs(info.outputs) do
			table.insert(keys, key)
		end
		table.sort(keys)
		for _, key in ipairs(keys) do
			output_slots:add(key, info.outputs[key])
			output_keys:add(info.outputs[key])
		end
	end
	return {
		stacks         = info.stacks,
		storage_slots  = info.storage_slots,
		work_slots     = info.work_slots,
		stack_max_size = info.stack_max_size,
		inputs         = info.inputs,
		input_initials = input_initials,
		output_slots   = output_slots,
		output_keys    = output_keys,
		on_progress    = info.on_progress,
		clobbers       = clobbers,
		voids          = voids,
		storage_slot_overhead_penalty = storage_slot_overhead_penalty,
		work_slot_overhead_penalty    = work_slot_overhead_penalty,
	}
end

local function lift(outputs, transform)
	local output_keys = ordered_map.make_ordered_map()
	for _, param in ipairs(outputs) do
		output_keys:add(param.node)
	end
	local lifted = {}
	local node_ids = id_store.make_id_store()
	local function get_lifted(param)
		return lifted[node_ids:get(param.node) .. "/" .. param.output_index]
	end
	ts_down(output_keys, function(expr)
		for output_index, param in ipairs(transform(expr, get_lifted)) do
			lifted[node_ids:get(expr) .. "/" .. output_index] = param
		end
	end)
	local lifted_outputs = {}
	for index, param in ipairs(outputs) do
		lifted_outputs[index] = get_lifted(param)
	end
	return lifted_outputs
end

local function fold_equivalent(outputs, output_slots, inputs)
	local expr_to_input_index = {}
	for index, expr in audited_pairs(inputs) do
		expr_to_input_index[expr] = index
	end
	local expr_to_output_slots = {}
	for slot, expr in output_slots:ipairs() do
		if not expr_to_output_slots[expr] then
			expr_to_output_slots[expr] = {}
		end
		table.insert(expr_to_output_slots[expr], slot)
	end
	local constants = {}
	local function get_constant(value)
		if not constants[value] then
			local constant = user_node.make_constant_(value)
			constant.output_slots_ = { {} }
			constant.label_ = ("%08X"):format(value)
			constants[value] = constant
		end
		return constants[value]
	end
	local node_ids = id_store.make_id_store()
	local output_ids = id_store.make_id_store()
	local composites = {}
	local function get_composite(expr)
		local lhs_id = output_ids:get(node_ids:get(expr.params_.lhs.node) .. "/" .. expr.params_.lhs.output_index)
		local rhs_id = output_ids:get(node_ids:get(expr.params_.rhs.node) .. "/" .. expr.params_.rhs.output_index)
		if expr.info_.commutative then
			lhs_id, rhs_id = math.min(lhs_id, rhs_id), math.max(lhs_id, rhs_id)
		end
		local composite_key = ("%i %i %i"):format(lhs_id, expr.info_.filt_tmp, rhs_id)
		if composites[composite_key] then
			composites[composite_key].label_ = composites[composite_key].label_ .. "\n" .. expr.label_
		else
			composites[composite_key] = expr
		end
		return composites[composite_key]
	end
	return lift(outputs, function(expr, get_lifted)
		local new_expr = setmetatable({}, user_node.mt_)
		for key, value in audited_pairs(expr) do
			new_expr[key] = value
		end
		new_expr.output_slots_ = { {} }
		if expr.type_ == "constant" then
			new_expr = get_constant(expr:constant_value_())
		elseif expr.type_ == "composite" then
			new_expr.params_ = {}
			local fold_to_constant = true
			local constant_params = {}
			for index, name in ipairs(expr.info_.params) do
				local new_param = get_lifted(expr.params_[name])
				if new_param.node.type_ == "constant" then
					constant_params[index] = new_param.node:constant_value_()
				else
					fold_to_constant = false
				end
				new_expr.params_[name] = new_param
			end
			if expr.info_.method == "filt_tmp" and not expr.marked_zeroable_ then
				if fold_to_constant then
					new_expr = get_constant(new_expr.info_.exec(unpack(constant_params)))
				else
					new_expr = get_composite(new_expr)
				end
			end
		end
		new_expr.user_node_ = expr
		new_expr.input_index_ = expr_to_input_index[expr] or false
		for _, slot in ipairs(expr_to_output_slots[expr] or {}) do
			table.insert(new_expr.output_slots_[1], slot)
		end
		return { {
			node             = new_expr,
			output_index     = 1,
		} }
	end)
end

local function flatten_selects(outputs)
	local lifted_select_groups = {}
	return lift(outputs, function(expr, get_lifted)
		local new_expr = setmetatable({}, user_node.mt_)
		for key, value in audited_pairs(expr) do
			new_expr[key] = value
		end
		local new_output_index = 1
		new_expr.output_slots_ = { {} }
		local output_slots_handled = false
		if expr.type_ == "composite" then
			new_expr.params_ = {}
			if expr.info_.method == "select" then
				new_expr.fed_value_ = {}
				local select_group = expr.select_group_ or { expr }
				if not lifted_select_groups[select_group] then
					local merged_info = {
						method    = "flat_select",
						params    = {},
						filt_tmps = {},
					}
					local param_index = 0
					local function insert(at, param_name, param)
						table.insert(merged_info.params, at, param_name)
						new_expr.params_[param_name] = param
					end
					local function insert_stage(param, filt_tmp)
						param_index = param_index + 1
						local param_name = "stage_" .. tostring(param_index)
						table.insert(merged_info.filt_tmps, 1, filt_tmp)
						insert(1, param_name, param)
					end
					local cond = get_lifted(expr.params_.cond)
					assert(cond.node.marked_zeroable_, "cond not marked zeroable")
					local curr = cond
					while curr.node.params_.rhs.node.marked_zeroable_ or curr.node.params_.lhs.node.marked_zeroable_ do
						assert(not (curr.node.params_.rhs.node.marked_zeroable_ and curr.node.params_.lhs.node.marked_zeroable_), "both rhs and lhs are marked zeroable")
						local function check(param, other)
							if curr.node.params_[param].node.marked_zeroable_ then
								insert_stage(curr.node.params_[other], curr.node.info_.filt_tmp)
								curr = curr.node.params_[param]
								return true
							end
							return false
						end
						if check("rhs", "lhs") then
							-- nothing
						elseif check("lhs", "rhs") then
							-- nothing
						end
					end
					insert_stage(curr.node.params_.rhs, curr.node.info_.filt_tmp)
					insert_stage(curr.node.params_.lhs, 0)
					merged_info.stages = #merged_info.params
					new_expr.info_ = merged_info
					merged_info.lanes = 0
					new_expr.output_count_ = 0
					local function add_lane(lane_expr)
						merged_info.lanes = merged_info.lanes + 1
						new_expr.output_count_ = new_expr.output_count_ + 1
						insert(merged_info.lanes * 2 - 1, ("lane_%i_vzero"):format(merged_info.lanes), get_lifted(lane_expr.params_.vzero))
						insert(merged_info.lanes * 2 - 1, ("lane_%i_vnonzero"):format(merged_info.lanes), get_lifted(lane_expr.params_.vnonzero))
						local output_slots = {}
						for _, slot in ipairs(lane_expr.output_slots_[1]) do
							table.insert(output_slots, slot)
						end
						new_expr.output_slots_[new_expr.output_count_] = output_slots
						if new_expr.fed_value_ then
							if lane_expr.fed_value_ then
								table.insert(new_expr.fed_value_, lane_expr.fed_value_[1])
							else
								new_expr.fed_value_ = false
							end
						end
						return new_expr.output_count_
					end
					lifted_select_groups[select_group] = {
						expr     = new_expr,
						add_lane = add_lane,
					}
				end
				local lifted_select_group = lifted_select_groups[select_group]
				new_expr = lifted_select_group.expr
				new_output_index = lifted_select_group.add_lane(expr)
				output_slots_handled = true
			else
				for index, name in ipairs(expr.info_.params) do
					new_expr.params_[name] = get_lifted(expr.params_[name])
				end
			end
		end
		if not output_slots_handled then
			for _, slot in ipairs(expr.output_slots_[1]) do
				table.insert(new_expr.output_slots_[1], slot)
			end
		end
		new_expr.user_node_ = expr.user_node_
		return { {
			node             = new_expr,
			output_index     = new_output_index,
		} }
	end)
end

local function derive_fed_values(output_keys)
	ts_down(output_keys, function(expr)
		if expr.type_ == "composite" then
			expr:derive_fed_value_()
		end
	end)
end

local function derive_occ_problems(raw_output_keys)
	local problems = {}
	hierarchy_up(raw_output_keys, function(leaf)
		if leaf.marked_occ_leaf_ then
			local domain = leaf.marked_occ_leaf_
			local leaf_map = ordered_map.make_ordered_map()
			leaf_map:add(leaf)
			local relevant = {}
			hierarchy_up(leaf_map, function(expr)
				relevant[expr] = true
				return true
			end)
			local closed = {}
			local clauses = {}
			local vars = {}
			local function alloc_var(comment)
				table.insert(vars, {
					comment = comment,
				})
				return #vars
			end
			local expr_bits = {}
			ts_down(raw_output_keys, function(expr)
				if not relevant[expr] then
					return
				end
				local expr_name = tostring(expr):gsub("[^A-Za-z0-9_]", "_")
				expr_bits[expr] = {}
				for i = 0, 31 do
					expr_bits[expr][i] = alloc_var(("%s.bit-%i"):format(expr_name, i))
				end
				local spec = expr.marked_occ_root_[domain]
				if spec then
					closed[expr] = true
					for i = 0, 31 do
						local function visit(spec_name, spec)
							local spec_var = alloc_var(spec_name)
							local branch_vars = {}
							for ix_branch, branch in ipairs(spec) do
								local branch_name = ("%s[%i]"):format(spec_name, ix_branch)
								local branch_var = alloc_var(branch_name)
								local fixed_bits = {}
								local mask = branch.mask or 0xFFFFFFFF
								for i = 0, 31 do
									if bitx.band(mask, bitx.lshift(1, i)) ~= 0 then
										if bitx.band(branch.fixed, bitx.lshift(1, i)) ~= 0 then
											table.insert(fixed_bits, expr_bits[expr][i])
										else
											table.insert(fixed_bits, -expr_bits[expr][i])
										end
									end
								end
								if branch.rest then
									table.insert(fixed_bits, visit(branch_name .. ".rest", branch.rest))
								end
								user_node.and_clauses_(clauses, branch_var, fixed_bits)
								table.insert(branch_vars, branch_var)
							end
							user_node.or_clauses_(clauses, spec_var, branch_vars)
							return spec_var
						end
						table.insert(clauses, { visit(("%s.root-spec"):format(expr_name), spec) })
					end
				elseif expr.type_ == "constant" then
					closed[expr] = true
					local value = expr:constant_value_()
					for i = 0, 31 do
						local set = bitx.band(bitx.rshift(value, i), 1) ~= 0
						if set then
							table.insert(clauses, { expr_bits[expr][i] })
						else
							table.insert(clauses, { -expr_bits[expr][i] })
						end
					end
				elseif expr.type_ == "composite" then
					local all_parents_closed = true
					local occ_params = {}
					for index, name in ipairs(expr.info_.params) do
						local parent = expr.params_[name]
						if not closed[parent.node] then
							all_parents_closed = false
						end
						table.insert(occ_params, expr_bits[parent.node])
					end
					if all_parents_closed then
						closed[expr] = true
						expr.info_.occ_clauses(expr_name, clauses, alloc_var, expr_bits[expr], unpack(occ_params))
					end
				end
			end)
			if not closed[leaf] then
				misc.user_error("leaf %s is not closed over requested offline correctness check domain %s", tostring(leaf), tostring(domain))
			end
			local bad = {}
			for i = 0, 31 do
				local keepalive_set = bitx.band(bitx.rshift(leaf.keepalive_, i), 1) ~= 0
				local payload_set   = bitx.band(bitx.rshift(leaf.payload_  , i), 1) ~= 0
				if keepalive_set then
					table.insert(bad, -expr_bits[leaf][i])
				elseif not payload_set then
					table.insert(bad, expr_bits[leaf][i])
				end
			end
			table.insert(clauses, bad)
			table.insert(problems, setmetatable({
				nvars   = #vars,
				clauses = clauses,
				leaf    = leaf,
			}, occ_problem_m))
		end
		return true
	end)
	return problems
end

local function preprocess_tree(output_keys, output_slots, inputs)
	local outputs = {}
	for key in output_keys:ipairs() do
		table.insert(outputs, {
			node         = key,
			output_index = 1,
		})
	end
	local raw_output_keys = ordered_map.make_ordered_map()
	for _, param in ipairs(outputs) do
		raw_output_keys:add(param.node)
	end
	derive_fed_values(raw_output_keys)
	local occ_problems = derive_occ_problems(raw_output_keys)
	outputs = fold_equivalent(outputs, output_slots, inputs)
	outputs = flatten_selects(outputs)
	return outputs, occ_problems
end

local function construct_layout(stacks, storage_slots, max_work_slots, stack_max_size, outputs, on_progress, clobbers_keys, voids_keys, storage_slot_overhead_penalty, work_slot_overhead_penalty, original_inputs, input_initials)
	local clobbers = {}
	for index in audited_pairs(clobbers_keys) do
		table.insert(clobbers, index)
	end
	local voids = {}
	for index in audited_pairs(voids_keys) do
		table.insert(voids, index)
	end
	local function constant_value(expr)
		return bitx.bor(expr.keepalive_, expr.payload_)
	end
	local output_keys = ordered_map.make_ordered_map()
	for _, param in ipairs(outputs) do
		output_keys:add(param.node)
	end
	local filt_tmps = {}
	local seen_life_3 = false
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "constant" then
			if constant_value(expr) == plot.LSNS_LIFE_3 then
				seen_life_3 = true
			end
		elseif expr.type_ == "composite" then
			if expr.info_.method == "filt_tmp" then
				filt_tmps[expr.info_.filt_tmp] = expr.info_
			end
		end
		return true
	end)
	local constants = {}
	local inputs = {}
	local composites = {}
	local index_to_expr = {}
	local function prepare_expr(expr)
		table.insert(index_to_expr, {
			index = #index_to_expr + 1,
			expr  = expr,
		})
		if expr.type_ == "constant" then
			table.insert(constants, expr)
		elseif expr.type_ == "input" then
			table.insert(inputs, expr)
		else
			table.insert(composites, expr)
		end
		return true
	end
	ts_down(output_keys, prepare_expr)
	if not seen_life_3 then
		prepare_expr(user_node.make_constant_(plot.LSNS_LIFE_3, 0))
	end
	local type_order = {
		constant  = 1,
		input     = 2,
		composite = 3,
	}
	table.sort(index_to_expr, function(lhs, rhs)
		local to_lhs, to_rhs = type_order[lhs.expr.type_], type_order[rhs.expr.type_]
		if to_lhs ~= to_rhs then
			return to_lhs < to_rhs
		end
		return lhs.index < rhs.index
	end)
	local source_index_to_expr = {}
	local param_to_index
	do
		local expr_to_index = {}
		local index = 1
		for _, value in ipairs(index_to_expr) do
			expr_to_index[value.expr] = index
			for i = 1, value.expr.output_count_ do
				source_index_to_expr[index] = value.expr
				index = index + 1
			end
		end
		function param_to_index(param)
			return expr_to_index[param.node] + param.output_index - 2
		end
	end
	local design_params = {
		stacks                        = stacks,
		work_slots                    = max_work_slots,
		stack_max_size                = stack_max_size,
		storage_slots                 = storage_slots,
		storage_slot_overhead_penalty = storage_slot_overhead_penalty,
		work_slot_overhead_penalty    = work_slot_overhead_penalty,
		constants                     = {},
		inputs                        = {},
		input_initials                = {},
		composites                    = {},
		outputs                       = {},
		clobbers                      = {},
		voids                         = {},
	}
	local source_index_to_output_slots = {}
	for _, param in ipairs(outputs) do
		local source_index = param_to_index(param)
		local output_slots = param.node.output_slots_[param.output_index]
		for _, output_slot in ipairs(output_slots) do
			table.insert(design_params.outputs, {
				source       = source_index,
				storage_slot = output_slot,
			})
		end
	end
	local function get_expr_index(expr, param_index)
		return param_to_index(expr.params_[expr.info_.params[param_index]])
	end
	for _, expr in ipairs(constants) do
		table.insert(design_params.constants, constant_value(expr))
	end
	for _, expr in ipairs(inputs) do
		table.insert(design_params.inputs, expr.input_index_ - 1)
		table.insert(design_params.input_initials, input_initials[original_inputs[expr.input_index_]] or 0xF000C0DE)
	end
	for _, expr in ipairs(composites) do
		if expr.info_.method == "filt_tmp" then
			table.insert(design_params.composites, {
				tmp     = expr.info_.filt_tmp,
				sources = {
					get_expr_index(expr, 1),
					get_expr_index(expr, 2),
				},
			})
		else
			local tmps = {}
			local sources = {}
			local need_work_slots = expr.info_.lanes * 2 + 2
			if need_work_slots > max_work_slots then
				misc.user_error("%s needs at least %i work slots in the worst case, only have %i", tostring(expr), need_work_slots, max_work_slots)
			end
			for j = 1, expr.info_.lanes do
				table.insert(sources, get_expr_index(expr, j * 2 - 1))
				table.insert(sources, get_expr_index(expr, j * 2))
			end
			for j = 1, expr.info_.stages do
				if j > 1 then
					table.insert(tmps, expr.info_.filt_tmps[j])
				end
				table.insert(sources, get_expr_index(expr, expr.info_.lanes * 2 + j))
			end
			table.insert(design_params.composites, {
				tmp         = 12,
				lane_count  = expr.info_.lanes,
				stage_count = expr.info_.stages,
				tmps        = tmps,
				sources     = sources,
			})
		end
	end
	for _, index in ipairs(clobbers) do
		table.insert(design_params.clobbers, index - 1)
	end
	for _, index in ipairs(voids) do
		table.insert(design_params.voids, index - 1)
	end
	return {
		design_params = design_params,
		debug_info    = setmetatable({
			output_keys          = output_keys,
			source_index_to_expr = source_index_to_expr,
		}, debug_info_m),
	}
end

local function deduplicate_locations(str)
	local locations = str:match("^user_node%[(.*)%]$")
	if locations then
		local deduplicated = {}
		local arr = {}
		for location in locations:gmatch("[^\n]+") do
			if not deduplicated[location] then
				deduplicated[location] = true
				table.insert(arr, location)
			end
		end
		return arr
	end
	return { str }
end

function debug_info_i:dump_stats(handle)
	local per_location = {}
	local function accumulate(location)
		local modname = location:match("^@(.*):%d+$")
		local key = modname or ""
		per_location[key] = (per_location[key] or 0) + 1
	end
	local constant_values = {}
	hierarchy_up(self.output_keys, function(expr)
		if expr.type_ == "constant" then
			table.insert(constant_values, bitx.bor(expr.keepalive_, expr.payload_))
		end
		if expr.type_ == "composite" then
			local locations = deduplicate_locations(tostring(expr))
			for _, location in ipairs(locations) do
				accumulate(location)
			end
		end
		return true
	end)
	local function make_tree_node()
		return {
			children = {},
			count    = 0,
		}
	end
	local tree = make_tree_node()
	local pad_to = 0
	for key, value in audited_pairs(per_location) do
		local curr = tree
		curr.count = curr.count + value
		for component in key:gmatch("[^%.]+") do
			if not curr.children[component] then
				curr.children[component] = make_tree_node()
			end
			curr = curr.children[component]
			curr.count = curr.count + value
			pad_to = math.max(pad_to, #component)
		end
	end
	local function print_rec(name, curr, level)
		assert(handle:write(("%s%s%s %i\n"):format((" "):rep(level * 2), name, (" "):rep(pad_to - #name), curr.count)))
		local arr = {}
		for key, value in audited_pairs(curr.children) do
			table.insert(arr, { key = key, value = value })
		end
		table.sort(arr, function(lhs, rhs)
			return lhs.value.count > rhs.value.count
		end)
		for _, item in ipairs(arr) do
			print_rec(item.key, item.value, level + 1)
		end
	end
	print_rec("*", tree, 0)
	assert(handle:write(("constants used: %i\n"):format(#constant_values)))
	table.sort(constant_values)
	for _, value in ipairs(constant_values) do
		assert(handle:write(("  %08X\n"):format(value)))
	end
end

function debug_info_i:dump_graph(handle, with_constants)
	local function to_dot_string(str)
		return ("\"%s\""):format(str:gsub("\"", "\\\""))
	end
	local node_ids = id_store.make_id_store()
	assert(handle:write("digraph {\n"))
	hierarchy_up(self.output_keys, function(expr)
		if with_constants or expr.type_ ~= "constant" then
			local locations = deduplicate_locations(tostring(expr))
			if expr.fed_value_ then
				for _, value in ipairs(expr.fed_value_) do
					table.insert(locations, ("=%08X"):format(value))
				end
			end
			assert(handle:write(("%i [label=%s]\n"):format(node_ids:get(expr), to_dot_string(table.concat(locations, "\n")))))
			assert(handle:write(("%i\n"):format(node_ids:get(expr))))
		end
		return true
	end)
	hierarchy_up(self.output_keys, function(expr)
		if expr.type_ == "composite" then
			for name, param in audited_pairs(expr.params_) do
				if with_constants or param.node.type_ ~= "constant" then
					assert(handle:write(("%i -> %s [label=%s]\n"):format(node_ids:get(param.node), node_ids:get(expr), to_dot_string(name))))
				end
			end
		end
		return true
	end)
	assert(handle:write("}\n"))
end

function debug_info_i:dump_work(handle, state)
	assert(handle:write(("%i %i\n"):format(#state.work_slot_states, state.work_slots)))
	for i = 1, #state.work_slot_states do
		local states = state.work_slot_states[i]
		for j = 1, state.work_slots do
			assert(handle:write(("%s %s\n"):format(tostring(states[j] and (states[j] - 1)), tostring(states[j] and self.source_index_to_expr[states[j]]))))
		end
	end
	assert(handle:write(("%i %i\n"):format(#state.storage_slot_states, state.storage_slots)))
	for i = 1, #state.storage_slot_states do
		local states = state.storage_slot_states[i]
		for j = 1, state.storage_slots do
			assert(handle:write(("%s %s\n"):format(tostring(states[j] and (states[j] - 1)), tostring(states[j] and self.source_index_to_expr[states[j]]))))
		end
	end
end

local build = misc.user_wrap(function(info)
	info = check_info(info)
	check_zeroness(info.output_keys)
	check_connectivity(info.output_keys, info.inputs)
	local outputs, occ_problems = preprocess_tree(info.output_keys, info.output_slots, info.inputs)
	local result = construct_layout(info.stacks, info.storage_slots, info.work_slots, info.stack_max_size, outputs, info.on_progress, info.clobbers, info.voids, info.storage_slot_overhead_penalty, info.work_slot_overhead_penalty, info.inputs, info.input_initials)
	result.debug_info.occ_problems = occ_problems
	return result
end)

return strict.make_mt_one("spaghetti.build", {
	build        = build,
	hierarchy_up = hierarchy_up,
})
