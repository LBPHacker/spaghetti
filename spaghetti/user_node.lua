local strict = require("spaghetti.strict")
local check  = require("spaghetti.check")
local misc   = require("spaghetti.misc")
local bitx   = require("spaghetti.bitx")

local function default_label()
	local frame_name = misc.user_frame_name()
	frame_name = misc.call_site_name(frame_name) or frame_name
	return ("@%s"):format(frame_name)
end

local occ_domain_m, occ_domain_i = strict.make_mt("spaghetti.user_node.occ_domain")

function occ_domain_m:__tostring()
	return ("occ_domain[%s]"):format(tostring(self.label_))
end

local function make_occ_domain(label)
	local occ_domain = setmetatable({
		label_ = default_label(),
	}, occ_domain_m)
	if label ~= nil then
		occ_domain:label(label)
	end
	return occ_domain
end

function occ_domain_i:label(label)
	self.label_ = label
	return self
end

local user_node_m, user_node_i = strict.make_mt("spaghetti.user_node.user_node")

user_node_m.__concat = misc.user_wrap(function(self, tbl)
	check.table("keepalive", tbl)
	return self:assert_(tbl[1], tbl[2])
end)

function user_node_m:__tostring()
	return ("user_node[%s]"):format(tostring(self.label_))
end

function user_node_i:never_zero()
	self.never_zero_ = true
	return self
end

function user_node_i:zeroable()
	self.marked_zeroable_ = true
	return self
end

local function check_spec(spec_name, spec, parent_defines)
	-- TODO: check pattern coverage somehow
	check.table(spec_name, spec)
	for ix_branch, branch in ipairs(spec) do
		local branch_name = ("%s[%i]"):format(spec_name, ix_branch)
		local payload = 0x00000000
		local mask    = 0xFFFFFFFF
		if branch.payload ~= nil then
			payload = branch.payload
		end
		if branch.mask ~= nil then
			mask = branch.mask
		end
		check.integer_range(branch_name .. ".fixed"  , branch.fixed, 0x00000000, 0xFFFFFFFF)
		check.integer_range(branch_name .. ".mask"   , mask        , 0x00000000, 0xFFFFFFFF)
		check.integer_range(branch_name .. ".payload", payload     , 0x00000000, 0xFFFFFFFF)
		local redefines = bitx.band(parent_defines, mask)
		if redefines ~= 0 then
			misc.user_error("%s redefines fixed bits %08X", branch_name .. ".mask", redefines)
		end
		local efmask = bitx.bor(parent_defines, mask)
		if bitx.band(payload, efmask) ~= 0 then
			misc.user_error("%s and %s share bits", branch_name .. ".payload", branch_name .. ".mask")
		end
		local defines = bitx.bor(payload, efmask)
		if defines ~= 0xFFFFFFFF then
			if branch.rest == nil then
				misc.user_error("%s only fixes bits %08X", branch_name, defines)
			end
			check_spec(branch_name .. ".rest", branch.rest, defines)
		end
	end
end

function user_node_i:occ_root(occ_domain, label, spec)
	check.mt(occ_domain_m, "occ_domain", occ_domain)
	if spec == "auto" then
		spec = { { fixed = self.keepalive_, mask = bitx.bxor(0xFFFFFFFF, self.payload_), payload = self.payload_ } }
	end
	check_spec("spec", spec, 0x00000000)
	self:label(label)
	self.marked_occ_root_[occ_domain] = spec
	return self
end

function user_node_i:can_be_zero()
	return self.keepalive_ == 0 and not self.never_zero_
end

function user_node_i:can_be_zero_indirectly()
	if self:can_be_zero() then
		return true
	end
	if self.type_ ~= "composite" then
		return false
	end
	if self.info_.method == "filt_tmp" then
		return self.params_.rhs.node:can_be_zero_indirectly() or
		       self.params_.rhs.node:can_be_zero_indirectly()
	elseif self.info_.method == "select" then
		return false
	else
		assert(false)
	end
end

function user_node_i:input_error_(name, message)
	local param_node = self.params_[name].node
	local param_output_index = self.params_[name].output_index
	misc.user_error(message:format(("input %s of %s connected to %s/%i"):format(name, tostring(self), tostring(param_node), param_output_index)))
end

function user_node_i:check_inputs_()
	if self.type_ ~= "composite" then
		assert(self.type_ == "input" or self.type_ == "constant")
		return
	end
	if self.info_.method == "filt_tmp" then
		-- self.params_.rhs is the filt value, unless self.info_.commutative, in which case either might be
		if self.marked_zeroable_ then
			-- at most one input may be marked zeroable
			if self.params_.lhs.node.marked_zeroable_ and self.params_.rhs.node.marked_zeroable_ then
				misc.user_error("both inputs of %s are marked zeroable", tostring(self))
			end
			-- if not self.info_.commutative, rhs must not be the zeroable input
			if not self.info_.commutative and self.params_.rhs.node.marked_zeroable_ then
				self:input_error_("rhs", "%s is marked zeroable")
			end
		else
			-- no input may be marked zeroable
			if self.params_.lhs.node.marked_zeroable_ then
				self:input_error_("lhs", "%s is marked zeroable")
			end
			if self.params_.rhs.node.marked_zeroable_ then
				self:input_error_("rhs", "%s is marked zeroable")
			end
		end
	elseif self.info_.method == "select" then
		if self.marked_zeroable_ then
			misc.user_error("%s marked zeroable despite being a select", tostring(self))
		end
		if not self.params_.cond.node.marked_zeroable_ then
			self:input_error_("cond", "%s is not marked zeroable")
		end
		if self.params_.vnonzero.node.marked_zeroable_ then
			self:input_error_("vnonzero", "%s is marked zeroable")
		end
		if self.params_.vzero.node.marked_zeroable_ then
			self:input_error_("vzero", "%s is marked zeroable")
		end
	else
		assert(false)
	end
end

function user_node_i:derive_fed_value_()
	assert(self.type_ == "composite")
	local params = {}
	for index, name in ipairs(self.info_.params) do
		local parent = self.params_[name].node
		if not parent.fed_value_ then
			return
		end
		table.insert(params, parent.fed_value_)
	end
	self.fed_value_ = self.info_.exec(unpack(params))
end

function user_node_i:constant_value_()
	assert(self.type_ == "constant")
	return bitx.bor(self.keepalive_, self.payload_)
end

function user_node_i:label(label)
	self.label_ = label
	return self
end

function user_node_i:tag(tag)
	self.tag_ = tag
	return self
end

local function check_keepalive_payload(keepalive, payload)
	check.keepalive("keepalive", keepalive)
	check.payload("payload", payload)
	if bitx.band(keepalive, payload) ~= 0 then
		misc.user_error("keepalive and payload share bits")
	end
	if bitx.bor(keepalive, payload) == 0 then
		misc.user_error("keepalive and payload are empty")
	end
end

function user_node_i:assert_(keepalive, payload)
	check_keepalive_payload(keepalive, payload)
	if self.keepalive_ ~= keepalive then
		misc.user_error("keepalive expected to be %08X, is actually %08X", keepalive, self.keepalive_)
	end
	if self.payload_ ~= payload then
		misc.user_error("payload expected to be %08X, is actually %08X", payload, self.payload_)
	end
	return self
end

user_node_i.assert = misc.user_wrap(user_node_i.assert_)

function user_node_i:force(keepalive, payload)
	check_keepalive_payload(keepalive, payload)
	self.keepalive_ = keepalive
	self.payload_ = payload
	return self
end

function user_node_i:relax_payload(payload)
	check_keepalive_payload(self.keepalive_, payload)
	self.payload_ = bitx.bor(self.payload_, payload)
	check_keepalive_payload(self.keepalive_, self.payload_)
	return self
end

function user_node_i:occ_force(occ_domain, label, keepalive, payload)
	check.mt(occ_domain_m, "occ_domain", occ_domain)
	self.marked_occ_leaf_ = occ_domain
	self:label(label)
	self:force(keepalive, payload)
	return self
end

function user_node_i:feed_(fed_value)
	if self.type_ ~= "input" then
		misc.user_error("only inputs can be fed")
	end
	check.integer("fed_value", fed_value)
	if bitx.band(fed_value, self.keepalive_) ~= self.keepalive_ or
	   bitx.band(fed_value, bitx.bor(self.keepalive_, self.payload_)) ~= fed_value then
		return nil, ("fed value %08X does not conform to keepalive/payload %08X/%08X"):format(fed_value, self.keepalive_, self.payload_)
	end
	self.fed_value_ = fed_value
	return self
end

user_node_i.feed = misc.user_wrap(user_node_i.feed_)

local function make_node(typev)
	return setmetatable({
		type_            = typev,
		never_zero_      = false,
		marked_zeroable_ = false,
		select_group_    = false,
		output_count_    = 1,
		tag_             = false,
		fed_value_       = false,
		marked_occ_root_ = {},
		marked_occ_leaf_ = false,
	}, user_node_m)
end

local function make_constant_(keepalive, payload)
	if payload == nil then
		payload = bitx.band(keepalive, bitx.bxor(check.keepalive_bits, check.payload_bits))
		keepalive = bitx.band(keepalive, check.keepalive_bits)
	end
	check_keepalive_payload(keepalive, payload)
	local node = make_node("constant")
	node.keepalive_  = keepalive
	node.payload_    = payload
	node.terminal_   = true
	node.label_      = default_label()
	node.fed_value_  = node:constant_value_()
	if bitx.band(bitx.bor(keepalive, payload), check.keepalive_bits) ~= 0 then
		node:never_zero()
	end
	return node
end

local make_constant = misc.user_wrap(make_constant_)

local function maybe_promote_number(thing)
	if type(thing) == "number" then
		thing = make_constant_(thing)
	end
	return thing
end

local function make_input_(keepalive, payload)
	check_keepalive_payload(keepalive, payload)
	local node = make_node("input")
	node.keepalive_  = keepalive
	node.payload_    = payload
	node.terminal_   = true
	node.label_      = default_label()
	return node
end

local make_input = misc.user_wrap(make_input_)

local function vonoff_forward(keepalive, payload)
	return keepalive, bitx.bxor(check.payload_bits, bitx.bor(keepalive, payload))
end

local function vonoff_backward(von, voff)
	return von, bitx.bxor(check.payload_bits, bitx.bor(von, voff))
end

local opnames = {}
local add_op
do
	local op_info_m = strict.make_mt("spaghetti.user_node.op_info_")
	function add_op(name, info)
		setmetatable(info, op_info_m)
		local function func(...)
			local nparams = select("#", ...)
			local params = { ... }
			local label = default_label()
			local node = make_node("composite")
			node.info_      = info
			node.params_    = {}
			for i = 1, #info.params do
				params[i] = maybe_promote_number(params[i])
				check.mt(user_node_m, info.params[i], params[i])
				node.params_[info.params[i]] = {
					node         = params[i],
					output_index = 1,
				}
			end
			local keepalive, payload = info.payload(unpack(params, 1, nparams))
			node.keepalive_  = keepalive
			node.payload_    = payload
			node.terminal_   = false
			node.label_      = label
			return node
		end
		user_node_i[name .. "_"] = func
		user_node_i[name] = misc.user_wrap(func)
		opnames[name] = info
	end
end

local function or_clauses(clauses, output, inputs)
	-- 1)  R == A | B | ...
	-- 2)  R => (A | B | ...)
	--     (A | B | ...) => R
	-- 3)  !R | (A | B | ...)
	--     !(A | B | ...) | R
	-- 4)  !R | A | B | ...
	--     (!A & !B & !...) | R
	-- 5)  !R | A | B | ...
	--     !A | R
	--     !B | R
	--     !... | R
	local long = { -output }
	for _, input in ipairs(inputs) do
		table.insert(long, input)
		table.insert(clauses, { output, -input })
	end
	table.insert(clauses, long)
end
local function and_clauses(clauses, output, inputs)
	-- 1)  R == A & B & ...
	-- 2)  (A & B & ...) => R
	--     R => (A & B & ...)
	-- 3)  !(A & B & ...) | R
	--     !R | (A & B & ...)
	-- 4)  (!A | !B | ...) | R
	--     !R | (A & B & ...)
	-- 5)  !A | !B | ... | R
	--     !R | A
	--     !R | B
	--     !R | ...
	local long = { output }
	for _, input in ipairs(inputs) do
		table.insert(long, -input)
		table.insert(clauses, { -output, input })
	end
	table.insert(clauses, long)
end
add_op("band", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		--   0 1 X
		-- 0 0 0 0
		-- 1 0 1 X
		-- X 0 X X
		local lhs_von, lhs_voff = vonoff_forward(lhs.keepalive_, lhs.payload_)
		local rhs_von, rhs_voff = vonoff_forward(rhs.keepalive_, rhs.payload_)
		local von = bitx.band(lhs_von, rhs_von)
		local voff = bitx.bor(lhs_voff, rhs_voff)
		return vonoff_backward(von, voff)
	end,
	exec = function(lhs, rhs)
		return bitx.band(lhs, rhs)
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		for i = 0, 31 do
			and_clauses(clauses, result[i], { lhs[i], rhs[i] })
		end
	end,
	method = "filt_tmp",
	filt_tmp = 1,
	commutative = true,
})
add_op("bor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		--   0 1 X
		-- 0 0 1 X
		-- 1 1 1 1
		-- X X 1 X
		local lhs_von, lhs_voff = vonoff_forward(lhs.keepalive_, lhs.payload_)
		local rhs_von, rhs_voff = vonoff_forward(rhs.keepalive_, rhs.payload_)
		local von = bitx.bor(lhs_von, rhs_von)
		local voff = bitx.band(lhs_voff, rhs_voff)
		return vonoff_backward(von, voff)
	end,
	exec = function(lhs, rhs)
		return bitx.bor(lhs, rhs)
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		for i = 0, 31 do
			or_clauses(clauses, result[i], { lhs[i], rhs[i] })
		end
	end,
	method = "filt_tmp",
	filt_tmp = 2,
	commutative = true,
})
add_op("bsub", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		--   0 1 X
		-- 0 0 0 0
		-- 1 1 0 X
		-- X X 0 X
		local lhs_von, lhs_voff = vonoff_forward(lhs.keepalive_, lhs.payload_)
		local rhs_von, rhs_voff = vonoff_forward(rhs.keepalive_, rhs.payload_)
		local von = bitx.band(lhs_von, rhs_voff)
		local voff = bitx.bor(lhs_voff, rhs_von)
		return vonoff_backward(von, voff)
	end,
	exec = function(lhs, rhs)
		return bitx.band(lhs, bitx.bxor(rhs, check.payload_bits))
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		for i = 0, 31 do
			and_clauses(clauses, result[i], { lhs[i], -rhs[i] })
		end
	end,
	method = "filt_tmp",
	filt_tmp = 3,
	commutative = false,
})
add_op("bxor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		--   0 1 X
		-- 0 0 1 X
		-- 1 1 0 X
		-- X X X X
		local lhs_von, lhs_voff = vonoff_forward(lhs.keepalive_, lhs.payload_)
		local rhs_von, rhs_voff = vonoff_forward(rhs.keepalive_, rhs.payload_)
		local von = bitx.bor(bitx.band(lhs_von, rhs_voff), bitx.band(lhs_voff, rhs_von))
		local voff = bitx.bor(bitx.band(lhs_voff, rhs_voff), bitx.band(lhs_von, rhs_von))
		return vonoff_backward(von, voff)
	end,
	exec = function(lhs, rhs)
		return bitx.bxor(lhs, rhs)
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		for i = 0, 31 do
			table.insert(clauses, { -result[i], lhs[i], rhs[i] })
			table.insert(clauses, { result[i], -lhs[i], rhs[i] })
			table.insert(clauses, { result[i], lhs[i], -rhs[i] })
			table.insert(clauses, { -result[i], -lhs[i], -rhs[i] })
		end
	end,
	method = "filt_tmp",
	filt_tmp = 7,
	commutative = true,
})
local function one_of(lhs, rhs)
	local keepalive = bitx.band(lhs.keepalive_, rhs.keepalive_)
	local payload = bitx.bor(lhs.payload_, rhs.payload_, bitx.bxor(lhs.keepalive_, rhs.keepalive_))
	return keepalive, payload
end
local function get_shift(value)
	local shift = 0
	for i = 0, 29 do
		if bitx.band(bitx.lshift(1, i), value) ~= 0 then
			return i
		end
	end
	return shift
end
local function get_shifts(rhs)
	local last = 29
	local shifts = {}
	for i = 0, 29 do
		if bitx.band(bitx.lshift(1, i), rhs.keepalive_) ~= 0 then
			last = i
			table.insert(shifts, i)
			break
		end
	end
	for i = 0, last - 1 do
		if bitx.band(bitx.lshift(1, i), rhs.payload_) ~= 0 then
			table.insert(shifts, i)
		end
	end
	if #shifts == 0 then
		table.insert(shifts, 0)
	end
	return shifts
end
local function get_shiftby(clauses, expr_name, alloc_var, rhs)
	local shiftby = {}
	for i = 0, 29 do
		shiftby[i] = alloc_var(("%s.shiftby-%i"):format(expr_name, i))
		local inputs = { rhs[i] }
		for j = 0, i - 1 do
			table.insert(inputs, -rhs[j])
		end
		and_clauses(clauses, shiftby[i], inputs)
	end
	local zero = {}
	for i = 0, 29 do
		table.insert(zero, -rhs[i])
	end
	local shiftby_0_zero = alloc_var(("%s.shiftby-0-zero"):format(expr_name))
	and_clauses(clauses, shiftby_0_zero, zero)
	local shiftby_0_full = alloc_var(("%s.shiftby-0-full"):format(expr_name))
	or_clauses(clauses, shiftby_0_full, { shiftby[0], shiftby_0_zero })
	shiftby[0] = shiftby_0_full
	return shiftby
end
local function do_shifts(lhs, rhs, func)
	local keepalive, payload
	for _, shift in ipairs(get_shifts(rhs)) do
		local i_keepalive = bitx.band(func(lhs.keepalive_, shift), check.shift_aware_bits)
		local i_payload = func(lhs.payload_, shift)
		if func == bitx.rshift and bitx.band(lhs.payload_, 0x80000000) ~= 0 then
			if bitx.band(bitx.bor(lhs.keepalive_, lhs.payload_), bitx.lshift(1, shift) - 1) ~= 0 then
				misc.user_error("shifting bits out of negative values is unpredicable")
			end
			-- ctype is an int; right shifts of signed types can be either arithmetic or logical in C++
			i_payload = bitx.bor(i_payload, bitx.lshift(0xFFFFFFFF, 32 - shift))
		end
		i_payload = bitx.band(i_payload, check.shift_aware_bits)
		if keepalive then
			keepalive, payload = one_of(
				{ keepalive_ = keepalive, payload_ = payload },
				{ keepalive_ = i_keepalive, payload_ = i_payload }
			)
		else
			keepalive = i_keepalive
			payload = i_payload
		end
	end
	return keepalive, payload
end
add_op("lshift", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		return do_shifts(lhs, rhs, bitx.lshift)
	end,
	exec = function(lhs, rhs)
		return bitx.band(bitx.lshift(lhs, get_shift(rhs)), check.shift_aware_bits)
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		local shiftby = get_shiftby(clauses, expr_name, alloc_var, rhs)
		for i = 0, 29 do
			local diagonals = {}
			for j = 0, i do
				local diagonal = alloc_var(("%s.diagonal-%i-%i"):format(expr_name, i, j))
				and_clauses(clauses, diagonal, { shiftby[j], lhs[i - j] })
				table.insert(diagonals, diagonal)
			end
			or_clauses(clauses, result[i], diagonals)
		end
		table.insert(clauses, { -result[30] })
		table.insert(clauses, { -result[31] })
		return clauses
	end,
	method = "filt_tmp",
	filt_tmp = 10,
	commutative = false,
})
add_op("rshift", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		return do_shifts(lhs, rhs, bitx.rshift)
	end,
	exec = function(lhs, rhs)
		return bitx.band(bitx.rshift(lhs, get_shift(rhs)), check.shift_aware_bits)
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, lhs, rhs)
		local shiftby = get_shiftby(clauses, expr_name, alloc_var, rhs)
		for i = 0, 29 do
			local diagonals = {}
			for j = 0, math.min(29, 31 - i) do
				local diagonal = alloc_var(("%s.diagonal-%i-%i"):format(expr_name, i, j))
				and_clauses(clauses, diagonal, { shiftby[j], lhs[i + j] })
				table.insert(diagonals, diagonal)
			end
			or_clauses(clauses, result[i], diagonals)
		end
		table.insert(clauses, { -result[30] })
		table.insert(clauses, { -result[31] })
		return clauses
	end,
	method = "filt_tmp",
	filt_tmp = 11,
	commutative = false,
})
add_op("select", {
	params = { "cond", "vnonzero", "vzero" },
	payload = function(cond, vnonzero, vzero)
		return one_of(vnonzero, vzero)
	end,
	exec = function(cond, vnonzero, vzero)
		if bitx.band(cond, 0x3FFFFFFF) == 0 then
			return vzero
		end
		return vnonzero
	end,
	occ_clauses = function(expr_name, clauses, alloc_var, result, cond, vnonzero, vzero)
		-- 1)  R = (S & A) | (!S & B)
		-- 2)  R => ((S & A) | (!S & B))
		--     ((S & A) | (!S & B)) => R
		-- 3)  !R | ((S & A) | (!S & B))
		--     !((S & A) | (!S & B)) | R
		-- 4)  !R | (S & A) | (!S & B)
		--     (!(S & A) & !(!S & B)) | R
		-- 5)  !R | (S & A) | (!S & B)
		--     ((!S | !A) & (S | !B)) | R
		-- 6)  !R | S | B
		--     !R | A | !S
		--     !R | A | B
		--     !S | !A | R
		--     S | !B | R
		local czero = {}
		for i = 0, 29 do
			table.insert(czero, -cond[i])
		end
		local select_0_czero = alloc_var(("%s.select-czero"):format(expr_name))
		and_clauses(clauses, select_0_czero, czero)
		for i = 0, 31 do
			table.insert(clauses, { -result[i], select_0_czero, vnonzero[i] })
			table.insert(clauses, { -result[i], vzero[i], -select_0_czero })
			table.insert(clauses, { -result[i], vzero[i], vnonzero[i] })
			table.insert(clauses, { -select_0_czero, -vzero[i], result[i] })
			table.insert(clauses, { select_0_czero, -vnonzero[i], result[i] })
		end
	end,
	method = "select",
})

return strict.make_mt_one("spaghetti.user_node", {
	make_occ_domain       = make_occ_domain,
	maybe_promote_number_ = maybe_promote_number,
	make_constant         = make_constant,
	make_constant_        = make_constant_,
	make_input            = make_input,
	make_input_           = make_input_,
	mt_                   = user_node_m,
	occ_domain_mt_        = occ_domain_m,
	opnames_              = opnames,
	or_clauses_           = or_clauses,
	and_clauses_          = and_clauses,
})
