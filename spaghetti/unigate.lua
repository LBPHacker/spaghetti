local bit = _G.bit or require("bit32")
local check = require("spaghetti.check")
local report = print

local LSNS_LIFE_3 = 0x10000003
local STACK_LIMIT = 1500
local unigate = {}

local function table_insert_all(dst, src)
	for _, item in ipairs(src) do
		table.insert(dst, item)
	end
	return dst
end

local node_i = {}
local node_m = { type = "node", __index = node_i }

local multinode_i = {}
local multinode_m = { type = "multinode", __index = multinode_i }

function multinode_i:merge_nodes(nodes)
	for node in pairs(nodes) do
		self.nodes[node] = true
	end
end

function multinode_m:__tostring()
	local node_strs = {}
	for node in pairs(self.nodes) do
		table.insert(node_strs, tostring(node))
	end
	return table.concat(node_strs, "|")
end

function multinode_i:id()
	local mt = getmetatable(self)
	setmetatable(self, nil)
	local str = tostring(self)
	setmetatable(self, mt)
	return str
end

local function make_multinode()
	return setmetatable({
		nodes = {},
		inputs = {},
		outputs = {},
	}, multinode_m)
end

local function check_index(entity, level, index, slots)
	level = level + 1
	check.positive_integer(entity, level, index)
	if index > slots then
		error(entity .. " is out of range", level)
	end
end

local function check_filt_ctype(entity, level, fctype)
	level = level + 1
	check.integer(entity, level, fctype)
	if fctype < 0 or fctype > 0x3FFFFFFF then
		error(entity .. " is out of range", level)
	end
end

local function check_keepalive(entity, level, keepalive)
	level = level + 1
	check_filt_ctype(entity, level, keepalive)
end

local function check_payload(entity, level, payload, keepalive)
	level = level + 1
	check_filt_ctype(entity, level, payload)
	if bit.band(payload, keepalive) ~= 0 then
		error(entity .. " shares bits with keepalive", level)
	end
end

local function check_node(entity, level, node)
	level = level + 1
	check.table(entity, level, node)
	if getmetatable(node) ~= node_m then
		error(entity .. " is not a node", level)
	end
end

local function check_input_node(entity, level, node)
	level = level + 1
	check_node(entity, level, node)
	if node.type_ ~= "input" then
		error(entity .. " is not an input node", level)
	end
end

local function check_output_node(entity, level, node)
	level = level + 1
	check_node(entity, level, node)
end

local function get_default_label(level)
	return "[@" .. (select(2, pcall(error, "@", level + 2)):match("^([^:]+:[^:]+): @$") or "???") .. "]"
end

local function constant(level, keepalive, payload)
	level = level + 1
	if payload == nil then
		payload = 0
	end
	check_keepalive("keepalive", level, keepalive)
	check_payload("payload", level, payload, keepalive)
	return setmetatable({
		type_ = "constant",
		keepalive_ = keepalive,
		payload_ = payload,
		label_ = payload == 0 and ("#%X"):format(keepalive) or ("#%X/%X"):format(keepalive, payload),
		constant_foldable_ = true,
		default_label_ = get_default_label(level),
	}, node_m)
end

function unigate.constant(...)
	local level = 2
	return constant(level - 1, ...) -- level adjusted due to tail call
end

do
	local function operator(info)
		local function func_inner(level, ...)
			level = level + 1
			if select("#", ...) ~= #info.operands then
				error("invalid number of operands", level)
			end
			local operands = { ... }
			-- local operand_strs = {}
			local constants_only = true
			for ix_operand = 1, #operands do
				if type(operands[ix_operand]) == "number" then
					operands[ix_operand] = constant(level, operands[ix_operand])
				end
				check_node("operand #" .. ix_operand, level, operands[ix_operand])
				-- operand_strs[ix_operand] = tostring(operands[ix_operand])
				local operand = operands[ix_operand]
				if info.name == "ternary" and info.operands[ix_operand] == "cond" then
					if operand.type_ ~= "operator" or #operand.operator_.operands ~= 2 then
						error("condition not provided by a binary operator", level)
					end
				else
					local keepalive = operand.keepalive_
					if operand.constant_foldable_ then
						keepalive = bit.bor(keepalive, operand.payload_)
					end
				end
				if not operand.constant_foldable_ then
					constants_only = false
				end
			end
			local keepalive, payload = info.apply(unpack(operands))
			return setmetatable({
				type_ = "operator",
				operator_ = info,
				operands_ = operands,
				keepalive_ = keepalive,
				payload_ = payload,
				-- label_ = ("%s(%s)"):format(info.name, table.concat(operand_strs, ",")),
				constant_foldable_ = constants_only,
				default_label_ = get_default_label(level),
			}, node_m)
		end

		local function func(...)
			local level = 2
			if info.name == "ternary" then
				local operands = { ... }
				local ternary_group = {}
				local results = {}
				while operands[2] do
					local result = func_inner(level, unpack(operands, 1, 3))
					result.ternary_group_ = ternary_group
					table.insert(results, result)
					table.remove(operands, 3)
					table.remove(operands, 2)
				end
				ternary_group.nodes = results
				return unpack(results)
			end
			return func_inner(level - 1, ...) -- level adjusted due to tail call
		end
		node_i[info.name] = func
		unigate[info.name] = func
		if info.metamethod then
			node_m[info.metamethod] = func
		end
	end

	local function lshiftk(bits, places)
		return bit.band(bit.lshift(bits, places), 0x3FFFFFFF)
	end

	local function rshiftk(bits, places)
		return bit.rshift(bits, places)
	end

	local function unshift(bits)
		local places = 0
		while bits > 1 do
			bits = bit.rshift(bits, 1)
			places = places + 1
		end
		return places
	end

	local function bit_bsub(lhs, rhs)
		return bit.band(lhs, bit.bxor(rhs, 0x3FFFFFFF))
	end

	local function get_shifts(keepalive, payload)
		if keepalive == 0 and payload == 0 then
			return { 0 }
		end
		local shifts = {}
		for shift = 0, 29 do
			if bit.band(keepalive, 1) ~= 0 then
				table.insert(shifts, shift)
				break
			end
			if bit.band(payload, 1) ~= 0 then
				table.insert(shifts, shift)
			end
			keepalive = bit.rshift(keepalive, 1)
			payload = bit.rshift(payload, 1)
		end
		return shifts
	end

	local function one_of(kapls)
		assert(#kapls > 0)
		local keepalive = 0x3FFFFFFF
		local keepdead = 0x3FFFFFFF
		local payload = 0x00000000
		for _, kapl in ipairs(kapls) do
			keepalive = bit.band(keepalive, kapl.keepalive_)
			keepdead = bit_bsub(bit_bsub(keepdead, kapl.payload_), kapl.keepalive_)
			payload = bit.bor(payload, kapl.payload_)
		end
		return {
			keepalive_ = keepalive,
			payload_ = bit_bsub(bit_bsub(0x3FFFFFFF, keepalive), keepdead),
		}
	end

	local function do_shifts(shift, lhs, rhs)
		local shifts = get_shifts(rhs.keepalive_, rhs.payload_)
		local kapls = {}
		for _, places in ipairs(shifts) do
			table.insert(kapls, {
				keepalive_ = shift(lhs.keepalive_, places),
				payload_ = shift(lhs.payload_, places),
			})
		end
		local one = one_of(kapls)
		return one.keepalive_, one.payload_
	end

	operator({
		name = "band",
		operands = { "lhs", "rhs" },
		metamethod = "__mod",
		commutative = true,
		filt_tmp = 1,
		apply = function(lhs, rhs)
			return
				bit.band(lhs.keepalive_, rhs.keepalive_),
				bit.bor(bit.band(lhs.payload_, rhs.payload_), bit.band(rhs.payload_, lhs.keepalive_), bit.band(lhs.payload_, rhs.keepalive_))
		end,
	})
	operator({
		name = "bor",
		operands = { "lhs", "rhs" },
		metamethod = "__add",
		commutative = true,
		filt_tmp = 2,
		apply = function(lhs, rhs)
			return
				bit.bor(lhs.keepalive_, rhs.keepalive_),
				bit_bsub(bit_bsub(bit.bor(lhs.payload_, rhs.payload_), lhs.keepalive_), rhs.keepalive_)
		end,
	})
	operator({
		name = "bsub",
		operands = { "lhs", "rhs" },
		metamethod = "__sub",
		commutative = true,
		filt_tmp = 3,
		apply = function(lhs, rhs)
			return
				bit_bsub(lhs.keepalive_, rhs.keepalive_),
				bit.bor(bit.band(lhs.payload_, rhs.payload_), bit.band(rhs.payload_, lhs.keepalive_), bit_bsub(lhs.payload_, rhs.keepalive_))
		end,
	})
	operator({
		name = "bxor",
		operands = { "lhs", "rhs" },
		metamethod = "__pow",
		commutative = true,
		filt_tmp = 7,
		apply = function(lhs, rhs)
			return
				bit_bsub(bit.bxor(lhs.keepalive_, rhs.keepalive_), bit.bor(lhs.payload_, rhs.payload_)),
				bit.bor(lhs.payload_, rhs.payload_)
		end,
	})
	operator({
		name = "lshift",
		operands = { "lhs", "rhs" },
		metamethod = "__mul",
		filt_tmp = 10,
		apply = function(lhs, rhs)
			return do_shifts(lshiftk, lhs, rhs)
		end,
	})
	operator({
		name = "rshift",
		operands = { "lhs", "rhs" },
		metamethod = "__div",
		filt_tmp = 11,
		apply = function(lhs, rhs)
			return do_shifts(rshiftk, lhs, rhs)
		end,
	})
	operator({
		name = "ternary",
		operands = { "cond", "vnonzero", "vzero" },
		apply = function(cond, vnonzero, vzero)
			if cond.keepalive_ ~= 0 then
				return vnonzero.keepalive_, vnonzero.payload_
			end
			if cond.keepalive_ == 0 and cond.payload_ == 0 then
				return vzero.keepalive_, vzero.payload_
			end
			local one = one_of({ vnonzero, vzero })
			return one.keepalive_, one.payload_
		end,
	})
end

function node_i:assert_(level, keepalive, payload)
	level = level + 1
	check_keepalive("keepalive", level, keepalive)
	check_payload("payload", level, payload, keepalive)
	if keepalive ~= self.keepalive_ then
		error(("keepalive expected to be 0x%08X, is actually 0x%08X"):format(keepalive, self.keepalive_), level)
	end
	if payload ~= self.payload_ then
		error(("payload expected to be 0x%08X, is actually 0x%08X"):format(payload, self.payload_), level)
	end
end

function node_i:assert(...)
	local level = 2
	return self:assert_(level - 1, ...) -- level adjusted due to tail call
end

function node_i:label(label)
	local level = 2
	local effective_label = label
	if type(label) == "table" then
		local offset = 1
		local tbl = label
		effective_label = nil
		if type(tbl[1]) == "string" then
			effective_label = tbl[1]
			offset = 2
		end
		if tbl[offset] then
			self:assert_(level, tbl[offset], tbl[offset + 1])
		end
	end
	self.label_ = effective_label
	return self
end
node_m.__concat = node_i.label

function node_m:__tostring()
	return self.label_ and tostring(self.label_) or self.default_label_
end

function unigate.input(keepalive, payload)
	local level = 2
	check_keepalive("keepalive", level, keepalive)
	check_payload("payload", level, payload, keepalive)
	return setmetatable({
		type_ = "input",
		keepalive_ = keepalive,
		payload_ = payload,
		default_label_ = get_default_label(level),
	}, node_m)
end

local function iterify(func)
	return function(data)
		local co = coroutine.create(function()
			func(data, coroutine.yield)
		end)
		return function()
			return select(2, assert(coroutine.resume(co)))
		end
	end
end

local function bfs(initial, visit)
	local seen = {}
	local to_visit = {}
	for _, node in ipairs(initial) do
		if not seen[node] then
			seen[node] = true
			table.insert(to_visit, node)
		end
	end
	while #to_visit > 0 do
		local next_to_visit = {}
		for _, node in ipairs(to_visit) do
			for _, next_node in ipairs(visit(node)) do
				if not seen[next_node] then
					seen[next_node] = true
					table.insert(next_to_visit, next_node)
				end
			end
		end
		to_visit = next_to_visit
	end
	return seen
end

local function ts(to_visit, visit, in_edge_count)
	local in_edge_counts = {}
	for _, node in ipairs(to_visit) do
		in_edge_counts[node] = in_edge_count(node)
		assert(in_edge_counts[node] == 0, "in edge count of initial node is non-zero")
	end
	bfs(to_visit, function(node)
		local to_visit = {}
		for _, next_node in ipairs(visit(node)) do
			if not in_edge_counts[next_node] then
				in_edge_counts[next_node] = in_edge_count(next_node)
			end
			assert(in_edge_counts[next_node] > 0, "in edge count of internal node is non-positive")
			in_edge_counts[next_node] = in_edge_counts[next_node] - 1
			if in_edge_counts[next_node] == 0 then
				table.insert(to_visit, next_node)
			end
		end
		return to_visit
	end)
end

local id_store_i = {}
local id_store_m = { type = "id_store", __index = id_store_i }

function id_store_i:get(obj, strict)
	if not self.ids_[obj] then
		assert(not strict, "object not present in store")
		table.insert(self.objs_, obj)
		self.ids_[obj] = #self.objs_
	end
	return self.ids_[obj]
end

function id_store_i:all()
	return self.objs_
end

local function make_id_store()
	return setmetatable({
		objs_ = {},
		ids_ = {},
	}, id_store_m)
end

local function check_inputs_reachable(level, inputs_reverse, outputs)
	level = level + 1
	local initial = {}
	for _, node in pairs(outputs) do
		table.insert(initial, node)
	end
	local seen_input = {}
	bfs(initial, function(node)
		local level = level + 2
		local to_visit = {}
		if node.type_ == "input" then
			if not inputs_reverse[node] then -- XXX: node classes?
				error("reached unlisted input node " .. tostring(node), level)
			end
			seen_input[node] = true
		end
		if node.type_ == "operator" then -- XXX: node classes?
			for index, operand in ipairs(node.operands_) do
				table.insert(to_visit, operand)
			end
		end
		return to_visit
	end)
	for node in pairs(inputs_reverse) do
		if not seen_input[node] then
			error("did not reach listed input node " .. tostring(node), level)
		end
	end
end

local function clone_hierarchy(inputs_reverse, outputs)
	-- this also prunes all remaining unreachable nodes
	local multinodes = {}
	local function get_multinode(node)
		if not multinodes[node] then
			local multinode = make_multinode()
			multinode.type_ = node.type_ -- XXX: node classes?
			multinode.keepalive_ = node.keepalive_
			multinode.payload_ = node.payload_
			multinode.operator_ = node.operator_
			multinode.ternary_group_ = node.ternary_group_
			multinode:merge_nodes({ [node] = true })
			multinodes[node] = multinode
		end
		return multinodes[node]
	end
	local hierarchy = {
		terminals = {},
		outputs = {},
	}
	local initial = {}
	for _, node in pairs(outputs) do
		table.insert(initial, node)
	end
	bfs(initial, function(node)
		local to_visit = {}
		local multinode = get_multinode(node)
		if node.type_ == "operator" then -- XXX: node classes?
			for ix_operand, operand in ipairs(node.operands_) do
				table.insert(to_visit, operand)
				local source = get_multinode(operand)
				table.insert(source.outputs, {
					multinode = multinode,
					operand = ix_operand,
				})
				multinode.inputs[ix_operand] = source
			end
		elseif node.type_ == "input" then
			multinode.input_index_ = inputs_reverse[node]
		end
		if node.type_ == "input" or node.type_ == "constant" then -- XXX: node classes?
			table.insert(hierarchy.terminals, multinode)
		end
		return to_visit
	end)
	for _, node in pairs(outputs) do
		table.insert(hierarchy.outputs, get_multinode(node))
	end
	return hierarchy
end

local function ts_hierarchy_down(hierarchy, visit)
	ts(hierarchy.terminals, function(multinode, dedges)
		local to_visit = {}
		visit(multinode, dedges)
		for _, output in ipairs(multinode.outputs) do
			table.insert(to_visit, output.multinode)
		end
		return to_visit
	end, function(multinode)
		return #multinode.inputs
	end)
end
local ts_hierarchy_down_iter = iterify(ts_hierarchy_down)

local function ts_hierarchy_up(hierarchy, visit)
	local initial = {}
	for _, multinode in ipairs(hierarchy.outputs) do
		if #multinode.outputs == 0 then
			table.insert(initial, multinode)
		end
	end
	ts(initial, function(multinode, dedges)
		local to_visit = {}
		visit(multinode, dedges)
		for _, source in ipairs(multinode.inputs) do
			table.insert(to_visit, source)
		end
		return to_visit
	end, function(multinode)
		return #multinode.outputs
	end)
end
local ts_hierarchy_up_iter = iterify(ts_hierarchy_up)

local function hierarchy_tie(destination, source, operand)
	destination.inputs[operand] = source
	table.insert(source.outputs, {
		multinode = destination,
		operand = operand,
	})
end

local function hierarchy_untie(destination, operand)
	local source = destination.inputs[operand]
	destination.inputs[operand] = nil
	for index, output in ipairs(source.outputs) do
		if output.multinode == destination then
			table.remove(source.outputs, index)
			break
		end
	end
end

local function do_folding(level, hierarchy)
	level = level + 1
	local new_hierarchy = {
		terminals = {},
		outputs = {},
	}
	local clones = {}
	for multinode in ts_hierarchy_down_iter(hierarchy) do
		local constants_only = true
		for _, input in ipairs(multinode.inputs) do
			if clones[input].type_ ~= "constant" then
				constants_only = false
			end
		end
		local clone
		if constants_only and multinode.type_ == "operator" then
			local operands = {}
			for ix_input, input in ipairs(multinode.inputs) do
				operands[ix_input] = clones[input]
			end
			local keepalive, payload = multinode.operator_.apply(unpack(operands))
			clone = make_multinode()
			clone.type_ = "constant"
			clone.keepalive_, clone.payload_ = keepalive, payload
		else
			clone = make_multinode()
			clone.type_ = multinode.type_ -- XXX: node classes?
			clone.keepalive_ = multinode.keepalive_
			clone.payload_ = multinode.payload_
			clone.operator_ = multinode.operator_
			clone.input_index_ = multinode.input_index_
			if multinode.type_ == "operator" and multinode.operator_.name == "ternary" then
				-- XXX: not exactly constant folding...
				local ternary_group = multinode.ternary_group_
				local cond = clones[multinode.inputs[1]]
				local vnz = clones[multinode.inputs[2]]
				local vz = clones[multinode.inputs[3]]
				if cond.inputs[1] then
					-- this is the first ternary in the group, redirect condition's inputs
					local function valid_cond(nnode)
						if nnode.type_ ~= "operator" then
							return false
						end
						if nnode.operator_.name == "ternary" then
							return false
						end
						return nnode.keepalive_ == 0
					end
					if not valid_cond(cond) then
						error("ternary condition " .. tostring(cond) .. " with non-zero keepalive", level)
					end
					local flat_cond = {}
					local flat_cond_tmps = {}
					local curr = cond
					while true do
						local valid_pred, valid_pred_index
						local function record_valid(level, pnode, index)
							level = level + 1
							local nnode = pnode.inputs[index]
							if not valid_cond(nnode) then
								return
							end
							if valid_pred then
								error("indirect ternary condition " .. tostring(pnode) .. " with multiple zero keepalive inputs", level)
							end
							valid_pred = nnode
							valid_pred_index = index
						end
						assert(#curr.inputs == 2)
						record_valid(level, curr, 1)
						if curr.operator_.commutative then
							record_valid(level, curr, 2)
						end
						assert(curr.operator_.filt_tmp)
						local function ternary_group_tie(filt_tmp, input)
							table.insert(flat_cond, {
								fold_input = input,
								filt_tmp = filt_tmp,
							})
							table.insert(flat_cond_tmps, filt_tmp)
							hierarchy_tie(clone, input, #flat_cond)
						end
						if valid_pred then
							ternary_group_tie(curr.operator_.filt_tmp, curr.inputs[3 - valid_pred_index])
						else
							ternary_group_tie(curr.operator_.filt_tmp, curr.inputs[2])
							ternary_group_tie(0, curr.inputs[1])
						end
						hierarchy_untie(curr, 1)
						hierarchy_untie(curr, 2)
						if not valid_pred then
							break
						end
						curr = valid_pred
					end
					ternary_group.flat_cond = flat_cond
					ternary_group.ternary_operator = table.concat(flat_cond_tmps)
				else
					-- this is not the first ternary in the group, get redirected inputs
					for ix_input, entry in ipairs(ternary_group.flat_cond) do
						hierarchy_tie(clone, entry.fold_input, ix_input)
					end
				end
				hierarchy_tie(clone, vnz, #ternary_group.flat_cond + 1)
				hierarchy_tie(clone, vz, #ternary_group.flat_cond + 2)
				clone.ternary_operator = ternary_group.ternary_operator
				clone.ternary_group_ = ternary_group
			else
				for ix_input, input in ipairs(multinode.inputs) do
					local source = clones[input]
					hierarchy_tie(clone, source, ix_input)
				end
			end
		end
		clone:merge_nodes(multinode.nodes)
		clones[multinode] = clone
	end
	for _, output in ipairs(hierarchy.outputs) do
		table.insert(new_hierarchy.outputs, clones[output])
	end
	for multinode in ts_hierarchy_up_iter(new_hierarchy) do
		if multinode.type_ == "input" or multinode.type_ == "constant" then -- XXX: node classes?
			table.insert(new_hierarchy.terminals, multinode)
		end
	end
	return new_hierarchy
end

local function check_nonzero(level, hierarchy)
	level = level + 1
	for multinode in ts_hierarchy_down_iter(hierarchy) do
		if not (multinode.type_ == "input" or multinode.type_ == "constant") then
			if multinode.keepalive_ == 0 then
				error("multinode " .. tostring(multinode) .. " with zero keepalive", level)
			end
		end
	end
end

local function unify_equivalent_siblings(hierarchy)
	-- not too extensive, really only merges operators of identical type and inputs; math
	-- says that this does not yield all identical nodes (in terms of logic expressions
	-- of their individual bits), but a fully symbolic solution would blow up due to XOR
	-- doubling clauses in both conjunctive and disjunctive normal forms, so this is good enough
	local new_hierarchy = {
		terminals = {},
		outputs = {},
	}
	local clones = {}
	local canonicals = {}
	local canonical_ids = make_id_store()
	for multinode in ts_hierarchy_down_iter(hierarchy) do
		local key
		if multinode.type_ == "operator" then -- XXX: node classes?
			key = ("operator %s"):format(multinode.operator_.name)
			if multinode.operator_.name == "ternary" then
				key = key .. (" %s"):format(multinode.ternary_operator)
			end
			for _, input in ipairs(multinode.inputs) do
				key = key .. (" %i"):format(canonical_ids:get(clones[input]))
			end
		elseif multinode.type_ == "constant" then
			multinode.constant = bit.bor(multinode.keepalive_, multinode.payload_)
			key = ("constant %i"):format(multinode.constant)
		elseif multinode.type_ == "input" then
			key = ("input %i"):format(multinode.input_index_)
		end
		if not canonicals[key] then
			local new_canonical = make_multinode()
			new_canonical.type_ = multinode.type_ -- XXX: node classes?
			new_canonical.constant = multinode.constant
			new_canonical.operator_ = multinode.operator_
			new_canonical.input_index_ = multinode.input_index_
			new_canonical.ternary_operator = multinode.ternary_operator
			new_canonical.ternary_group_ = multinode.ternary_group_
			for ix_input, input in ipairs(multinode.inputs) do
				local source = clones[input]
				hierarchy_tie(new_canonical, source, ix_input)
			end
			canonicals[key] = new_canonical
		end
		local clone = canonicals[key]
		clone:merge_nodes(multinode.nodes)
		clones[multinode] = clone
	end
	for _, output in ipairs(hierarchy.outputs) do
		table.insert(new_hierarchy.outputs, clones[output])
	end
	for multinode in ts_hierarchy_up_iter(new_hierarchy) do
		if multinode.type_ == "input" or multinode.type_ == "constant" then -- XXX: node classes?
			table.insert(new_hierarchy.terminals, multinode)
		end
	end
	return new_hierarchy
end

local storage_i = {}
local storage_m = { type = "storage", __index = storage_i }

function storage_i:alloc(slot, ignore)
	if slot then
		if self.used_[slot] then
			return
		end
		self.used_[slot] = true
		return slot
	end
	for index = 1, #self.used_ do
		if not self.used_[index] and not (ignore and ignore[index]) then
			self.used_[index] = true
			return index
		end
	end
end

function storage_i:free(slot)
	assert(self.used_[slot])
	self.used_[slot] = false
end

local function make_storage(available_slots)
	local used = {}
	for _ = 1, available_slots do
		table.insert(used, false)
	end
	return setmetatable({
		used_ = used,
	}, storage_m)
end

local queue_i = {}
local queue_m = { type = "queue", __index = queue_i }

function queue_i:push(item)
	self.items_[self.head_] = item
	self.head_ = self.head_ + 1
	self.used_ = self.used_ + 1
end

function queue_i:auto_skip_()
	while not self.items_[self.tail_] do
		self.tail_ = self.tail_ + 1
	end
end

function queue_i:get()
	self:auto_skip_()
	return self.items_[self.tail_]
end

function queue_i:pop()
	assert(not self:empty())
	self:auto_skip_()
	self.items_[self.tail_] = nil
	self.tail_ = self.tail_ + 1
	self.used_ = self.used_ - 1
end

function queue_i:oo_get(cursor)
	assert(self.items_[cursor])
	return self.items_[cursor]
end

function queue_i:oo_pop(cursor)
	assert(self.items_[cursor])
	self.items_[cursor] = nil
	self.used_ = self.used_ - 1
end

function queue_i:empty()
	return self.used_ == 0
end

function queue_i:inspect()
	local seen = 0
	local cursor = self.tail_
	return function()
		if seen == self.used_ then
			return
		end
		while not self.items_[cursor] do
			cursor = cursor + 1
		end
		local item_cursor, item = cursor, self.items_[cursor]
		cursor = cursor + 1
		seen = seen + 1
		return item_cursor, item
	end
end

function queue_i:requeue(other)
	while not other:empty() do
		self:push(other:get())
		other:pop()
	end
end

local function make_queue()
	return setmetatable({
		head_ = 1,
		tail_ = 1,
		used_ = 0,
		items_ = {},
	}, queue_m)
end

local type_value = {
	[ "input" ] = 1,
	[ "constant" ] = 2,
}
local function sort_terminals(inputs_reverse, hierarchy)
	local terminals = {}
	local have_life_3 = false
	for _, multinode in ipairs(hierarchy.terminals) do
		table.insert(terminals, multinode)
		if multinode.type_ == "constant" and multinode.constant == LSNS_LIFE_3 then
			have_life_3 = true
		end
	end
	if not have_life_3 then
		table.insert(terminals, {
			type_ = "constant",
			nodes = { false },
			outputs = {},
			constant = LSNS_LIFE_3,
		})
	end
	table.sort(terminals, function(lhs, rhs)
		local lhst = type_value[lhs.type_]
		local rhst = type_value[rhs.type_]
		if lhst ~= rhst then return lhst < rhst end
		if lhs.type_ == "constant" then
			if lhs.constant ~= rhs.constant then return lhs.constant < rhs.constant end
			return false
		end
		if lhs.type_ == "input" then
			local lhsn = inputs_reverse[next(lhs.nodes)]
			local rhsn = inputs_reverse[next(rhs.nodes)]
			if lhsn ~= rhsn	then return lhsn < rhsn end
			return false
		end
	end)
	return terminals
end

local function layer_markup(level, inputs_reverse, outputs, outputs_reverse, hierarchy, computation_slots, storage_slots)
	level = level + 1
	for multinode in ts_hierarchy_down_iter(hierarchy) do
		multinode.dependency_count = #multinode.inputs
	end
	local nodes_ready = make_queue() -- other than ternaries
	local ternary_groups_ready = make_queue()
	local nodes_ready_next = make_queue()
	local ternary_groups_ready_next = make_queue()

	local function cslots_needed_for_ternary()
		local lowest = math.huge
		local index
		for ix_ternary_group, ternary_group in ternary_groups_ready:inspect() do
			local cslots_needed = #ternary_group.flat_cond + #ternary_group.multinodes * 2
			if lowest > cslots_needed then
				lowest = cslots_needed
				index = ix_ternary_group
			end
		end
		return lowest, index
	end

	local constants = {}
	local layers = {}
	local storage = make_storage(storage_slots)

	for multinode in ts_hierarchy_down_iter(hierarchy) do
		multinode.use_count = 0
		local ternary_group = multinode.ternary_group_
		if ternary_group then
			if not ternary_group.multinodes then
				ternary_group.multinodes = {}
				ternary_group.multinodes_pending = 0
			end
			table.insert(ternary_group.multinodes, multinode)
			ternary_group.multinodes_pending = ternary_group.multinodes_pending + 1
		end
	end

	local mark_processed
	do
		local node_ids = make_id_store()
		for _, multinode in ipairs(sort_terminals(inputs_reverse, hierarchy)) do
			node_ids:get(multinode)
			nodes_ready:push(multinode)
			multinode.layer = 0
			multinode.enabled = true
		end

		function mark_processed(multinode)
			multinode.processed = true
			local enabled = {}
			for _, output in ipairs(multinode.outputs) do
				local destination = output.multinode
				destination.dependency_count = destination.dependency_count - 1
				if destination.dependency_count == 0 then
					table.insert(enabled, destination)
				end
			end
			table.sort(enabled, function(lhs, rhs)
				if lhs.type_ ~= rhs.type_ then return lhs.type_ < rhs.type_ end
				if lhs.type_ == "operator" then
					local lhso = lhs.operator_.name
					local rhso = rhs.operator_.name
					if lhso ~= rhso then return lhso < rhso end
					if lhs.operator_.name == "ternary" then
						local lhsto = lhs.ternary_operator
						local rhsto = rhs.ternary_operator
						if lhsto ~= trhso then return lhsto < rhsto end
					end
				end
				for index = 1, math.max(#lhs.inputs, #rhs.inputs) do
					if not lhs.inputs[index] then return  true end
					if not rhs.inputs[index] then return false end
					local lhsi = node_ids:get(lhs.inputs[index], true)
					local rhsi = node_ids:get(rhs.inputs[index], true)
					if lhsi ~= rhsi then return lhsi < rhsi end
				end
				return false
			end)
			for _, emnode in ipairs(enabled) do
				emnode.enabled = true
				if emnode.type_ == "operator" and emnode.operator_.name == "ternary" then
					local ternary_group = emnode.ternary_group_
					ternary_group.multinodes_pending = ternary_group.multinodes_pending - 1
					if ternary_group.multinodes_pending == 0 then
						ternary_groups_ready_next:push(ternary_group)
					end
				else
					nodes_ready_next:push(emnode)
				end
				node_ids:get(emnode)
			end
		end
	end

	local remapped_outputs = {}
	local function remap_outputs(multinode)
		for node in pairs(multinode.nodes) do
			if outputs_reverse[node] then
				for _, index in ipairs(outputs_reverse[node]) do
					if multinode.storage_slot ~= index then
						table.insert(remapped_outputs, {
							from = multinode.storage_slot,
							to = index,
						})
					end
				end
			end
		end
	end

	local function to_storage(level, multinode, tentative)
		level = level + 1
		if not multinode.storage_slot then
			if inputs_reverse[next(multinode.nodes)] then
				multinode.storage_slot = assert(storage:alloc(inputs_reverse[next(multinode.nodes)]))
			end
			if not multinode.storage_slot then
				local incides = {}
				for node in pairs(multinode.nodes) do
					if outputs_reverse[node] then
						for _, index in ipairs(outputs_reverse[node]) do
							table.insert(incides, index)
						end
					end
				end
				table.sort(incides)
				for _, index in ipairs(incides) do
					multinode.storage_slot = storage:alloc(index)
					if multinode.storage_slot then
						break
					end
				end
			end
			if not multinode.storage_slot then
				multinode.storage_slot = storage:alloc(nil, outputs)
			end
			if not multinode.storage_slot then
				error("insufficient variable storage", level)
			end
			if multinode.type_ == "operator" then
				local last_layer = layers[#layers]
				last_layer[#last_layer].store_to = multinode.storage_slot
			end
			remap_outputs(multinode)
			multinode.use_count = 1
		else
			assert(multinode.use_count > 0)
			multinode.use_count = multinode.use_count + 1
		end
	end

	local chaining_output
	local function chain_to_storage(level)
		level = level + 1
		to_storage(level, chaining_output.multinode.inputs[chaining_output.operand])
		chaining_output = nil
	end

	local function decrease_use_count(multinode)
		assert(multinode.use_count > 0)
		multinode.use_count = multinode.use_count - 1
		if multinode.use_count == 0 then
			storage:free(multinode.storage_slot)
			multinode.storage_slot = nil
		end
	end

	local function produce_output(level, multinode)
		level = level + 1
		multinode.layer = #layers
		for node in pairs(multinode.nodes) do
			if outputs_reverse[node] then
				to_storage(level, multinode)
			end
		end
		for index, output in ipairs(multinode.outputs) do
			local destination = output.multinode
			local max_other_layers = 0
			for _, input in ipairs(destination.inputs) do
				if input ~= multinode then
					if input.layer then
						max_other_layers = math.max(max_other_layers, input.layer)
					else
						max_other_layers = math.huge
					end
				end
			end
			if max_other_layers >= #layers then
				to_storage(level, multinode)
			elseif multinode.operator_.name == "ternary" then
				to_storage(level, multinode)
			elseif destination.operator_.name == "ternary" then
				to_storage(level, multinode)
			elseif output.operand ~= 1 and not destination.operator_.commutative then
				to_storage(level, multinode)
			elseif chaining_output then
				to_storage(level, multinode)
			else
				chaining_output = output
			end
		end
		mark_processed(multinode)
	end

	local cslots_left
	local layer_open = false
	local function open_layer()
		if not layer_open then
			cslots_left = computation_slots
			layer_open = true
			table.insert(layers, {})
		end
		return layers[#layers]
	end
	local function close_layer()
		if layer_open then
			layer_open = false
		end
	end

	while not nodes_ready:empty() or not ternary_groups_ready:empty() do
		local layer_filled = false
		local emitted_something = false
		while not nodes_ready:empty() and not layer_filled do
			local candidate = nodes_ready:get()
			assert(candidate.enabled)
			emitted_something = true
			if candidate.type_ == "input" then
				for _ in ipairs(candidate.outputs) do
					to_storage(level, candidate)
				end
				mark_processed(candidate)
				nodes_ready:pop()
			elseif candidate.type_ == "constant" then
				to_storage(level, candidate)
				candidate.use_count = math.huge
				table.insert(constants, {
					store_to = candidate.storage_slot,
					value = candidate.constant,
				})
				mark_processed(candidate)
				nodes_ready:pop()
			elseif candidate.processed then
				nodes_ready:pop()
			else
				local pop_first_candidate = true
				while true do
					assert(not candidate.processed)
					local cslots_needed
					if chaining_output then
						-- at this point it's guaranteed that candidate can accept a chained input
						assert(#candidate.inputs == 2)
						cslots_needed = 1
					else
						assert(#candidate.inputs == 2)
						cslots_needed = 2
					end
					if not ternary_groups_ready:empty() then
						cslots_needed = cslots_needed + cslots_needed_for_ternary()
					end
					local last_layer = open_layer()
					if cslots_needed > cslots_left then
						layer_filled = true
						break -- syntactically out of the candidate loop, but logically out of the ready node loop
					end
					if chaining_output then
						for index, input in ipairs(candidate.inputs) do
							if index ~= chaining_output.operand then
								assert(input.storage_slot)
								table.insert(last_layer, {
									load_from = input.storage_slot,
									filt_tmp = candidate.operator_.filt_tmp,
								})
								cslots_left = cslots_left - 1
								decrease_use_count(input)
							end
						end
						chaining_output = nil
					else
						assert(candidate.inputs[1].storage_slot)
						table.insert(last_layer, {
							load_from = candidate.inputs[1].storage_slot,
							filt_tmp = 0,
						})
						decrease_use_count(candidate.inputs[1])
						assert(candidate.inputs[2].storage_slot)
						table.insert(last_layer, {
							load_from = candidate.inputs[2].storage_slot,
							filt_tmp = candidate.operator_.filt_tmp,
						})
						decrease_use_count(candidate.inputs[2])
						cslots_left = cslots_left - 2
					end
					produce_output(level, candidate)
					if pop_first_candidate then
						pop_first_candidate = false
						nodes_ready:pop()
					end
					if not chaining_output then
						break -- out of the candidate loop
					end
					candidate = chaining_output.multinode
				end
			end
		end
		if chaining_output then
			chain_to_storage(level)
		end
		if not ternary_groups_ready:empty() then
			local last_layer = open_layer()
			local cslots_needed, ix_ternary_group = cslots_needed_for_ternary()
			local ternary_group = ternary_groups_ready:oo_get(ix_ternary_group)
			if cslots_left >= cslots_needed then
				local multiplicity = #ternary_group.multinodes
				local vnz_index = #ternary_group.flat_cond + 1
				local vz_index = #ternary_group.flat_cond + 2
				local first_ternary = ternary_group.multinodes[1]
				local tentative_store_to_later = {}
				for _, ternary in ipairs(ternary_group.multinodes) do
					assert(ternary.inputs[vz_index].storage_slot)
					table.insert(last_layer, {
						load_from = ternary.inputs[vz_index].storage_slot,
						filt_tmp = 0,
					})
					cslots_left = cslots_left - 1
					decrease_use_count(ternary.inputs[vz_index])
					tentative_store_to_later[ternary] = last_layer[#last_layer]
					last_layer[#last_layer].store_to = nil
				end
				for ix_input = #ternary_group.flat_cond, 1, -1 do
					local input = first_ternary.inputs[ix_input]
					assert(input.storage_slot)
					table.insert(last_layer, {
						load_from = input.storage_slot,
						filt_tmp = ternary_group.flat_cond[ix_input].filt_tmp,
					})
					cslots_left = cslots_left - 1
					for _ = 1, multiplicity do
						decrease_use_count(input)
					end
				end
				for _, ternary in ipairs(ternary_group.multinodes) do
					assert(ternary.inputs[vnz_index].storage_slot)
					table.insert(last_layer, {
						load_from = ternary.inputs[vnz_index].storage_slot,
						filt_tmp = 0,
					})
					decrease_use_count(ternary.inputs[vnz_index])
					cslots_left = cslots_left - 1
					produce_output(level, ternary)
					last_layer[#last_layer].store_to = ternary.storage_slot
					tentative_store_to_later[ternary].tentative_store_to = ternary.storage_slot
				end
				emitted_something = true
				ternary_groups_ready:oo_pop(ix_ternary_group)
			end
		end
		if not emitted_something then
			error("insufficient computation slots", level)
		end
		close_layer()
		nodes_ready:requeue(nodes_ready_next)
		ternary_groups_ready:requeue(ternary_groups_ready_next)
	end

	for multinode in ts_hierarchy_down_iter(hierarchy) do
		assert(multinode.processed)
		if multinode.type_ ~= "constant" then
			local expected_use_count = 0
			for node in pairs(multinode.nodes) do
				if outputs_reverse[node] then
					expected_use_count = expected_use_count + 1
				end
			end
			assert(multinode.use_count == expected_use_count)
		end
	end

	table.sort(remapped_outputs, function(lhs, rhs)
		return lhs.to < rhs.to
	end)
	for _, remap in ipairs(remapped_outputs) do
		if cslots_left == 0 then
			layer_open = false
		end
		local last_layer = open_layer()
		table.insert(last_layer, {
			load_from = remap.from,
			filt_tmp = 0,
			store_to = remap.to,
		})
		cslots_left = cslots_left - 1
	end
	close_layer()
	return constants, layers
end

local sim = rawget(_G, "sim")
if not sim then
	sim = {
		PMAPBITS = 9,
	}
end
local elem = rawget(_G, "elem")
if not elem then
	local element_ids = 0
	elem = setmetatable({}, { __index = function(_, key)
		element_ids = element_ids + 1
		elem[key] = element_ids
		return element_ids
	end })
end
local pt = setmetatable({}, { __index = function(_, key)
	return elem["DEFAULT_PT_" .. key]
end })

local particle_macros = {
	[ "lcap" ] = function(ctx)
		return {
			{ type = pt.FILT, x = -ctx.LEFT_13 - 1, ctype = LSNS_LIFE_3 },
			{ type = pt.DMND, x = -ctx.LEFT_13 },
		}
	end,	
	[ "lfilt" ] = function(ctx, offset)
		return {
			{ type = pt.FILT, x = -offset * 2 + 2 },
		}
	end,	
	[ "rfilt" ] = function(ctx, offset, value)
		return {
			{ type = pt.FILT, x = offset + 2 + ctx.STACKS * 3, ctype = value },
		}
	end,	
	[ "bottom" ] = function(ctx, stack)
		return {
			{ type = pt.FILT, x = stack * 3 - 1 },
			{ type = pt.CONV, x = stack * 3 + 1, tmp = pt.SPRK, ctype = pt.PSCN },
			{ type = pt.CONV, x = stack * 3 + 1, tmp = pt.PSCN, ctype = pt.SPRK },
			{ type = pt.SPRK, x = stack * 3 + 1, ctype = pt.PSCN, life = 4 },
		}
	end,
	[ "top" ] = function(ctx, stack)
		return {
			{ type = pt.STOR, x = stack * 3 },
		}
	end,	
	[ "mode" ] = function(ctx, stack, tmp)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in cr p3
			{ type = pt.CRAY, x = stack * 3, ctype = pt.SPRK, tmp = 1 },
			--          .. cr p3
			{ type = pt.CRAY, x = stack * 3, ctype = bit.bor(pt.FILT, bit.lshift(tmp, sim.PMAPBITS)), tmp = 1 },
			--          fi    p3
		}
	end,
	[ "load" ] = function(ctx, stack, from, to)
		return {
			--          fi ld p3
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from + 1 + (ctx.STACKS - stack) * 3 },
			--          fi dr p3
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to * 2 - 4 + stack * 3 },
			--          fi    p3
		}
	end,
	[ "cload" ] = function(ctx, stack, to)
		return {
			--          fi dr p3
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to * 2 - 4 + stack * 3 },
			--          fi    p3
		}
	end,
	[ "tstore" ] = function(ctx, stack, from, to)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from * 2 + stack * 3 - 2 },
			--          p3    fi
		}
	end,
	[ "store" ] = function(ctx, stack, from, to)
		return {
			--          p3 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = from * 2 + stack * 3 - 2 },
			--          p3 dr fi
			{ type = pt.DRAY, x = stack * 3, tmp = 1, tmp2 = to + (ctx.STACKS - stack) * 3 },
			--          p3    fi
		}
	end,
	[ "aray" ] = function(ctx, stack)
		return {
			--          fi co p3
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.INST },
			--          fi co it
			{ type = pt.CONV, x = stack * 3, tmp = pt.INST, ctype = pt.SPRK },
			--          fi ld i4
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.RIGHT_13 + (ctx.STACKS - stack) * 3 + 1 },
			--          fi ls i4
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          fi ar i3
			{ type = pt.ARAY, x = stack * 3 },
			--          fi    i3
		}
	end,
	[ "clear" ] = function(ctx, stack)
		return {
			--          fi cr p3
			{ type = pt.CRAY, x = stack * 3, ctype = pt.SPRK, tmp2 = stack * 3 - 3 },
			--          fi    p3
		}
	end,
	[ "east" ] = function(ctx, stack)
		return {
			--          fi co ?3
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          in co ?3
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          in co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          ps co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          p4 ld fi
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.LEFT_13 + stack * 3 },
			--          p4 ls fi
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          p3    fi
		}
	end,
	[ "west" ] = function(ctx, stack)
		return {
			--          p3 co fi
			{ type = pt.CONV, x = stack * 3, tmp = pt.FILT, ctype = pt.INSL },
			--          p3 co in
			{ type = pt.CONV, x = stack * 3, tmp = pt.SPRK, ctype = pt.FILT },
			--          fi co in
			{ type = pt.CONV, x = stack * 3, tmp = pt.INSL, ctype = pt.PSCN },
			--          fi co ps
			{ type = pt.CONV, x = stack * 3, tmp = pt.PSCN, ctype = pt.SPRK },
			--          fi ld p4
			{ type = pt.LDTC, x = stack * 3, tmp = 1, life = ctx.RIGHT_13 + (ctx.STACKS - stack) * 3 + 1 },
			--          fi ls p4
			{ type = pt.LSNS, x = stack * 3, tmp = 3, tmp2 = 1 },
			--          fi    p3
		}
	end,
}
local function lift_to_parts(level, constants, layers, computation_slots, storage_slots, stacks, x, y, extra_parts)
	level = level + 1

	local store_tentative_score = {
		[ "tstore" ] = 1,
		[ "store"  ] = 2,
	}

	local right_13
	for _, constant in ipairs(constants) do
		if constant.value == LSNS_LIFE_3 then
			right_13 = constant.store_to
		end
	end
	local ctx = {
		LEFT_13 = computation_slots * 2,
		RIGHT_13 = right_13,
		STACKS = stacks,
	}
	local function push_part(tbl, part)
		part.x = (part.x or 0) + x
		part.y = (part.y or 0) + y
		table.insert(tbl, part)
	end
	local function materialize_macro(tbl, name, ...)
		for _, part in ipairs(particle_macros[name](ctx, ...)) do
			push_part(tbl.parts, part)
		end
		table.insert(tbl.macros, { name, ... })
	end
	local function make_parts()
		return {
			parts = {},
			macros = {},
		}
	end
	local function parts_push_all(dst, src)
		table_insert_all(dst.parts, src.parts)
		table_insert_all(dst.macros, src.macros)
	end

	local parts = make_parts()
	if extra_parts then
		for _, part in ipairs(extra_parts) do
			push_part(parts.parts, part)
		end
	end

	materialize_macro(parts, "lcap")
	for ix_cslot = 1, computation_slots do
		materialize_macro(parts, "lfilt", ix_cslot)
	end
	local constant_values = {}
	for ix_sslot = 1, storage_slots do
		constant_values[ix_sslot] = 0
	end
	for _, constant in ipairs(constants) do
		constant_values[constant.store_to] = constant.value
	end
	for ix_sslot = 1, storage_slots do
		materialize_macro(parts, "rfilt", ix_sslot, constant_values[ix_sslot])
	end
	local ix_layer = 1
	for ix_stack = 1, stacks do
		local parts_bottom = make_parts()
		materialize_macro(parts_bottom, "bottom", ix_stack)
		local parts_top = make_parts()
		materialize_macro(parts_top, "top", ix_stack)
		local parts_left = STACK_LIMIT - #parts_bottom - #parts_top
		parts_push_all(parts, parts_bottom)
		while layers[ix_layer] do
			local parts_layer = make_parts()
			local by_modes = {}
			for index, item in ipairs(layers[ix_layer]) do
				item.index = index
				if not by_modes[item.filt_tmp] then
					by_modes[item.filt_tmp] = {}
				end
				table.insert(by_modes[item.filt_tmp], item)
			end
			for mode = 11, 0, -1 do
				if by_modes[mode] then
					table.sort(by_modes[mode], function(lhs, rhs)
						return lhs.load_from < rhs.load_from
					end)
					materialize_macro(parts_layer, "mode", ix_stack, mode)
					local last_load_from
					for _, item in ipairs(by_modes[mode]) do
						if item.load_from == last_load_from then
							materialize_macro(parts_layer, "cload", ix_stack, item.index)
						else
							materialize_macro(parts_layer, "load", ix_stack, item.load_from, item.index)
						end
						last_load_from = item.load_from
					end
				end
			end
			materialize_macro(parts_layer, "aray", ix_stack)
			materialize_macro(parts_layer, "east", ix_stack)
			local stores = {}
			for _, item in ipairs(layers[ix_layer]) do
				if item.store_to then
					table.insert(stores, {
						macro = "store",
						stack = ix_stack,
						from = item.index,
						to = item.store_to,
					})
				end
				if item.tentative_store_to then
					table.insert(stores, {
						macro = "tstore",
						stack = ix_stack,
						from = item.index,
						to = item.tentative_store_to,
					})
				end
			end
			table.sort(stores, function(lhs, rhs)
				if lhs.to ~= rhs.to then return lhs.to < rhs.to end
				local lhs_t_score = store_tentative_score[lhs.macro]
				local rhs_t_score = store_tentative_score[rhs.macro]
				if lhs_t_score ~= rhs_t_score then return lhs_t_score < rhs_t_score end
				return false
			end)
			for _, store in ipairs(stores) do
				materialize_macro(parts_layer, store.macro, store.stack, store.from, store.to)
			end
			materialize_macro(parts_layer, "west", ix_stack)
			materialize_macro(parts_layer, "clear", ix_stack)
			local parts_needed = #parts_layer
			if parts_left < parts_needed then
				-- try again in the next stack
				break
			end
			parts_left = parts_left - parts_needed
			parts_push_all(parts, parts_layer)
			ix_layer = ix_layer + 1
		end
		parts_push_all(parts, parts_top)
	end
	if layers[ix_layer] then
		error("too few stacks", level)
	end
	-- for _, macro in ipairs(parts.macros) do
	-- 	report(table.concat(macro, " "))
	-- end
	return parts.parts, parts.macros
end

local function emit(parts, stacks, x, y)
	if not rawget(_G, "tpt") then
		report("not running inside TPT, cannot emit particles")
		return
	end
	local props = {}
	for key in pairs(sim) do
		local prop = key:match("^FIELD_(.*)$")
		if prop then
			props[prop:lower()] = true
		end
	end
	for index, part in ipairs(parts) do
		part.k = index
	end
	table.sort(parts, function(lhs, rhs)
		if lhs.y ~= rhs.y then return lhs.y < rhs.y end
		if lhs.x ~= rhs.x then return lhs.x < rhs.x end
		if lhs.k ~= rhs.k then return lhs.k < rhs.k end
		return false
	end)
	local ids = {}
	for _ in ipairs(parts) do
		local id = sim.partCreate(-3, 4, 4, pt.DMND)
		if id == -1 then
			for _, kid in ipairs(ids) do
				sim.partKill(kid)
			end
			error("failed to spawn enough particles", level)
		end
		table.insert(ids, id)
	end
	table.sort(ids)
	for ix_part, part in ipairs(parts) do
		local id = ids[ix_part]
		sim.partProperty(id, "type", part.type)
		for key, value in pairs(part) do
			if props[key] and key ~= "type" then
				sim.partProperty(id, key, value)
			end
		end
	end
	for ix_stack = 1, stacks do
		sim.createWalls(x + ix_stack * 3, y, 1, 1, 12)
	end
	report("done")
end

function unigate.synthesize(inputs, outputs, computation_slots, storage_slots, stacks, x, y, extra_parts)
	x = x - 6
	local level = 2
	check.table("inputs", level, inputs)
	for index, node in pairs(inputs) do
		check_index("input index " .. tostring(index), level, index, storage_slots)
		check_input_node("input node at index " .. tostring(index), level, node)
	end
	check.table("outputs", level, outputs)
	for index, node in pairs(outputs) do
		check_index("output index " .. tostring(index), level, index, storage_slots)
		check_output_node("output node at index " .. tostring(index), level, node)
	end
	check.positive_integer("computation slots", level, computation_slots)
	check.positive_integer("storage slots", level, storage_slots)
	local inputs_reverse = {}
	for index, node in pairs(inputs) do
		if inputs_reverse[node] then
			error("input index " .. tostring(index) .. " clashes with input index " .. inputs_reverse[node], level)
		end
		inputs_reverse[node] = index
	end
	local outputs_reverse = {}
	for index, node in pairs(outputs) do
		if not outputs_reverse[node] then
			outputs_reverse[node] = {}
		end
		table.insert(outputs_reverse[node], index)
	end
	check_inputs_reachable(level, inputs_reverse, outputs)
	local hierarchy = clone_hierarchy(inputs_reverse, outputs)
	hierarchy = do_folding(level, hierarchy)
	check_nonzero(level, hierarchy)
	hierarchy = unify_equivalent_siblings(hierarchy)
	local constants, layers = layer_markup(level, inputs_reverse, outputs, outputs_reverse, hierarchy, computation_slots, storage_slots)
	local parts = lift_to_parts(level, constants, layers, computation_slots, storage_slots, stacks, x, y, extra_parts)
	-- report(#parts - (extra_parts and #extra_parts or 0))
	emit(parts, stacks, x, y)
end

function unigate.shift(k)
	return bit.lshift(1, k)
end

function unigate.lshiftk(a, b)
	return unigate.lshift(a, unigate.shift(b))
end

function unigate.rshiftk(a, b)
	return unigate.rshift(a, unigate.shift(b))
end

return unigate
