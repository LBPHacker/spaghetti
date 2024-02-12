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
					neighbours[param] = true
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
				children[parent][expr] = true
				parents[expr][parent] = true
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

local function lift(output_keys, transform)
	local lifted = {}
	ts_down(output_keys, function(expr)
		lifted[expr] = transform(expr, lifted)
	end)
	local lifted_output_keys = {}
	for key in pairs(output_keys) do
		lifted_output_keys[lifted[key]] = true
	end
	return lifted_output_keys
end

local function fold_equivalent(output_keys)
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
	return lift(output_keys, function(expr, lifted)
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
				local new_param = lifted[expr.params_[name]]
				if new_param.type_ == "constant" then
					constant_params[index] = get_constant_value(new_param)
				else
					fold_to_constant = false
				end
				new_expr.params_[name] = new_param
			end
			if fold_to_constant and expr.info_.method == "filt_tmp" then
				new_expr = get_constant(new_expr.info_.exec(unpack(constant_params)))
			end
		end
		new_expr.user_node_ = expr
		return new_expr
	end)
end

local function preprocess_tree(output_keys)
	output_keys = fold_equivalent(output_keys)
	return output_keys
end

local storage_slot_overhead_penalty = 10
local commit_cost                   = 18
local load_cost                     =  2
local cload_cost                    =  1
local mode_cost                     =  2
local store_cost                    =  2
local cstore_cost                   =  1

local function construct_layout(stacks, storage_slots, max_work_slots, output_keys, on_progress)
	local outputs = 0
	local filt_tmps = {}
	hierarchy_up(output_keys, function(expr)
		if expr.type_ == "composite" then
			if expr.info_.method == "filt_tmp" then
				filt_tmps[expr.info_.filt_tmp] = expr.info_
			end
		end
		if output_keys[expr] then
			outputs = outputs + 1
		end
		return true
	end)
	local constants = {}
	local inputs = {}
	local composites = {}
	local index_to_expr = {}
	ts_down(output_keys, function(expr)
		table.insert(index_to_expr, {
			index = #index_to_expr + 1,
			expr = expr,
		})
		if expr.type_ == "constant" then
			table.insert(constants, expr)
		elseif expr.type_ == "input" then
			table.insert(inputs, expr)
		else
			table.insert(composites, expr)
		end
		return true
	end)
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
	local expr_to_index = {}
	for key, value in pairs(index_to_expr) do
		expr_to_index[value.expr] = key
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
	io.stdout:write(("%f %i %i %i %i %i %i\n"):format(
		storage_slot_overhead_penalty,
		commit_cost,
		load_cost,
		cload_cost,
		mode_cost,
		store_cost,
		cstore_cost
	))
	io.stdout:write(("%i %i %i %i\n"):format(#constants, #inputs, #composites, outputs))
	for i = 1, #index_to_expr do
		local expr = index_to_expr[i].expr
		if expr.type_ == "composite" then
			if expr.info_.method == "filt_tmp" then
				io.stdout:write(("%i %i %i\n"):format(
					filt_tmp_to_index[expr.info_.filt_tmp] - 1,
					expr_to_index[expr.params_.rhs] - 1,
					expr_to_index[expr.params_.lhs] - 1
				))
			else
				local cond = expr.params_.cond
				assert(cond.marked_zeroable_, "cond not marked zeroable")
				assert(not cond.params_.rhs.marked_zeroable_, "cond.rhs marked zeroable")
				assert(not cond.params_.lhs.marked_zeroable_, "cond.lhs marked zeroable")
				io.stdout:write(("%i 1 2 %i %i %i %i %i\n"):format(
					#index_to_filt_tmp, expr_to_index[expr.params_.vnonzero] - 1,
					expr_to_index[expr.params_.vzero] - 1,
					expr_to_index[cond.params_.rhs] - 1,
					filt_tmp_to_index[cond.info_.filt_tmp] - 1,
					expr_to_index[cond.params_.lhs] - 1
				))
			end
		end
	end
	for expr in pairs(output_keys) do
		io.stdout:write(("%i "):format(expr_to_index[expr] - 1))
	end
	io.stdout:write("\n")
end

local function build(info)
	return misc.user_wrap(function()
		info = check_info(info)
		check_zeroness(info.output_keys)
		check_connectivity(info.output_keys, info.inputs)
		local output_keys = preprocess_tree(info.output_keys)
		construct_layout(info.stacks, info.storage_slots, info.work_slots, output_keys, info.on_progress)
	end)
end

return strict.make_mt_one("spaghetti.build", {
	build = build,
})
