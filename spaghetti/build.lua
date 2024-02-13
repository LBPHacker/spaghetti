local strict = require("spaghetti.strict")
strict.wrap_env()

local check     = require("spaghetti.check")
local user_node = require("spaghetti.user_node")
local graph     = require("spaghetti.graph")
local misc      = require("spaghetti.misc")
local id_store  = require("spaghetti.id_store")

local function hierarchy_up(output_keys, visit)
	graph.bfs(output_keys, function(expr)
		local neighbours = {}
		if visit(expr) then
			if expr.type_ == "composite" then
				for _, param in pairs(expr.params_) do
					neighbours[param.node] = true
				end
			end
		end
		return neighbours
	end)
end

local function ts_down(output_keys, visit)
	local children = {}
	local parents = {}
	local initial = {}
	hierarchy_up(output_keys, function(expr)
		children[expr] = {}
		parents[expr] = {}
		return true
	end)
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "composite" then
			for _, parent in pairs(expr.params_) do
				children[parent.node][expr] = true
				parents[expr][parent.node] = true
			end
		end
		if expr.type_ == "input" or expr.type_ == "constant" then
			initial[expr] = true
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
		if expr.type_ == "composite" then
			expr:check_inputs_()
		end
		if output_keys[expr] then
			if expr.marked_zeroable_ then
				misc.user_error("output %s is marked zeroable", tostring(expr))
			end
		end
		return true
	end)
end

local function check_connectivity(output_keys, inputs)
	local seen = {}
	hierarchy_up(output_keys, function(expr)
		seen[expr] = true
		return true
	end)
	for key, input in pairs(inputs) do
		if not seen[input] then
			local valuename = "info.inputs[" .. key .. "]"
			misc.user_error("%s is not connected", valuename)
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
	if info.on_progress ~= nil then
		check.func("info.on_progress", info.on_progress)
	end
	check.table("info.inputs", info.inputs)
	for key, value in pairs(info.inputs) do
		local keyname = "info.inputs key " .. tostring(key)
		check.integer(keyname, key)
		if key < 0 or key > info.storage_slots then
			misc.user_error("%s is out of bounds", keyname)
		end
		local valuename = "info.inputs[" .. key .. "]"
		check.mt(user_node.mt_, valuename, value)
		if value.type_ ~= "input" then
			misc.user_error("%s is not an input", valuename)
		end
	end
	check.table("info.outputs", info.outputs)
	for key, value in pairs(info.outputs) do
		local keyname = "info.outputs key " .. tostring(key)
		check.integer(keyname, key)
		if key < 0 or key > info.storage_slots then
			misc.user_error("%s is out of bounds", keyname)
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
		for key, value in pairs(info.clobbers) do
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
	return {
		stacks        = info.stacks,
		storage_slots = info.storage_slots,
		work_slots    = info.work_slots,
		inputs        = info.inputs,
		output_keys   = misc.values_to_keys(info.outputs),
		on_progress   = info.on_progress,
		clobbers      = clobbers,
	}
end

local function lift(outputs, transform)
	local output_keys = {}
	for _, param in ipairs(outputs) do
		output_keys[param.node] = true
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
	for index, param in pairs(outputs) do
		lifted_outputs[index] = get_lifted(param)
	end
	return lifted_outputs
end

local function fold_equivalent(outputs)
	local function get_constant_value(expr)
		return bit.bor(expr.keepalive_, expr.payload_)
	end
	local constants = {}
	local function get_constant(value)
		if not constants[value] then
			local constant = user_node.make_constant_(value)
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
			composites[composite_key].label_ = composites[composite_key].label_ .. "+" .. expr.label_
		else
			composites[composite_key] = expr
		end
		return composites[composite_key]
	end
	return lift(outputs, function(expr, get_lifted)
		local new_expr = setmetatable({}, user_node.mt_)
		for key, value in pairs(expr) do
			new_expr[key] = value
		end
		if expr.type_ == "constant" then
			new_expr = get_constant(get_constant_value(expr))
		elseif expr.type_ == "composite" then
			new_expr.params_ = {}
			local fold_to_constant = true
			local constant_params = {}
			for index, name in pairs(expr.info_.params) do
				local new_param = get_lifted(expr.params_[name])
				if new_param.node.type_ == "constant" then
					constant_params[index] = get_constant_value(new_param.node)
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
		return { {
			node         = new_expr,
			output_index = 1,
		} }
	end)
end

local function flatten_selects(outputs)
	local lifted_select_groups = {}
	return lift(outputs, function(expr, get_lifted)
		local new_expr = setmetatable({}, user_node.mt_)
		for key, value in pairs(expr) do
			new_expr[key] = value
		end
		local new_output_index = 1
		if expr.type_ == "composite" then
			new_expr.params_ = {}
			if expr.info_.method == "select" then
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
								curr = curr.node.params_[param]
								insert_stage(curr.node.params_[other], curr.node.info_.filt_tmp)
							end
						end
						check("rhs", "lhs")
						check("lhs", "rhs")
					end
					insert_stage(curr.node.params_.lhs, curr.node.info_.filt_tmp)
					insert_stage(curr.node.params_.rhs, 0) -- rhs comes first
					merged_info.stages = #merged_info.params
					new_expr.info_ = merged_info
					merged_info.lanes = 0
					new_expr.output_count_ = 0
					local function add_lane(lane_expr)
						merged_info.lanes = merged_info.lanes + 1
						new_expr.output_count_ = new_expr.output_count_ + 1
						insert(merged_info.lanes * 2 - 1, ("lane_%i_vzero"):format(merged_info.lanes), get_lifted(lane_expr.params_.vzero))
						insert(merged_info.lanes * 2 - 1, ("lane_%i_vnonzero"):format(merged_info.lanes), get_lifted(lane_expr.params_.vnonzero))
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
			else
				for index, name in pairs(expr.info_.params) do
					new_expr.params_[name] = get_lifted(expr.params_[name])
				end
			end
		end
		new_expr.user_node_ = expr.user_node_
		return { {
			node         = new_expr,
			output_index = new_output_index,
		} }
	end)
end

local function preprocess_tree(output_keys)
	local outputs = {}
	for key in pairs(output_keys) do
		table.insert(outputs, {
			node         = key,
			output_index = 1,
		})
	end
	outputs = fold_equivalent(outputs)
	outputs = flatten_selects(outputs)
	return outputs
end

local storage_slot_overhead_penalty = 10
local LSNS_LIFE_3                   = 0x10000003

local function construct_layout(stacks, storage_slots, max_work_slots, outputs, on_progress)
	local function constant_value(expr)
		return bit.bor(expr.keepalive_, expr.payload_)
	end
	local output_keys = {}
	for _, param in ipairs(outputs) do
		output_keys[param.node] = true
	end
	local filt_tmps = {}
	local seen_life_3 = false
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "constant" then
			if constant_value(expr) == LSNS_LIFE_3 then
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
	if not seen_life_3 then
		prepare_expr(user_node.make_constant_(LSNS_LIFE_3, 0))
	end
	ts_down(output_keys, prepare_expr)
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
	local param_to_index
	do
		local expr_to_index = {}
		local index = 1
		for _, value in ipairs(index_to_expr) do
			expr_to_index[value.expr] = index
			index = index + value.expr.output_count_
		end
		function param_to_index(param)
			return expr_to_index[param.node] + param.output_index - 2
		end
	end
	local index_to_filt_tmp = {}
	local filt_tmp_to_index = {}
	for key in pairs(filt_tmps) do
		table.insert(index_to_filt_tmp, key)
		filt_tmp_to_index[key] = #index_to_filt_tmp
	end
	io.stdout:write(("%i %i %i\n"):format(#index_to_filt_tmp, max_work_slots, storage_slots))
	for i = 1, #index_to_filt_tmp do
		io.stdout:write(("%i "):format(filt_tmps[index_to_filt_tmp[i]].commutative and 1 or 0))
	end
	io.stdout:write("\n")
	io.stdout:write(("%f\n"):format(storage_slot_overhead_penalty))
	io.stdout:write(("%i %i %i %i\n"):format(#constants, #inputs, #composites, #outputs))
	for _, expr in ipairs(constants) do
		io.stdout:write(("%i "):format(constant_value(expr)))
	end
	io.stdout:write("\n")
	for _, expr in ipairs(composites) do
		local function get_expr_index(param_index)
			return param_to_index(expr.params_[expr.info_.params[param_index]])
		end
		if expr.info_.method == "filt_tmp" then
			io.stdout:write(("%i %i %i\n"):format(
				filt_tmp_to_index[expr.info_.filt_tmp] - 1,
				get_expr_index(2), -- rhs comes first
				get_expr_index(1)
			))
		else
			io.stdout:write(("%i %i %i"):format(
				#index_to_filt_tmp,
				expr.info_.lanes,
				expr.info_.stages
			))
			for j = 1, expr.info_.lanes do
				io.stdout:write((" %i %i"):format(
					get_expr_index(j * 2 - 1),
					get_expr_index(j * 2)
				))
			end
			for j = 1, expr.info_.stages do
				if j > 1 then
					io.stdout:write((" %i"):format(filt_tmp_to_index[expr.info_.filt_tmps[j]] - 1))
				end
				io.stdout:write((" %i"):format(get_expr_index(expr.info_.lanes * 2 + j)))
			end
			io.stdout:write("\n")
		end
	end
	for _, param in ipairs(outputs) do
		io.stdout:write(("%i "):format(param_to_index(param)))
	end
	io.stdout:write("\n")
end

local function build(info)
	return misc.user_wrap(function()
		info = check_info(info)
		check_zeroness(info.output_keys)
		check_connectivity(info.output_keys, info.inputs)
		local outputs = preprocess_tree(info.output_keys)
		construct_layout(info.stacks, info.storage_slots, info.work_slots, outputs, info.on_progress)
	end)
end

return strict.make_mt_one("spaghetti.build", {
	build = build,
})
