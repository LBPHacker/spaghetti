local strict = require("spaghetti.strict")
strict.wrap_env()

local check = require("spaghetti.check")
local misc  = require("spaghetti.misc")
local bitx  = require("spaghetti.bitx")

local user_node_m, user_node_i = strict.make_mt("spaghetti.user_node.user_node")

function user_node_m:__concat(tbl)
	return misc.user_wrap(function()
		check.table("keepalive", tbl)
		return self:assert_(tbl[1], tbl[2])
	end)
end

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

function user_node_i:can_be_zero()
	return self.keepalive_ == 0 and not self.never_zero_
end

function user_node_i:input_error_(name, message)
	local param_node = self.params_[name].node
	local param_output_index = self.params_[name].output_index
	misc.user_error(message:format(("input %s of %s connected to %s/%i"):format(name, tostring(self), tostring(param_node), param_output_index)))
end

function user_node_i:check_inputs_()
	if not self.marked_zeroable_ and self:can_be_zero() then
		misc.user_error("%s can be zero despite not being marked zeroable", tostring(self))
	end
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

function user_node_i:label(label)
	self.label_ = label
	return self
end

function user_node_i:assert_(keepalive, payload)
	if self.keepalive_ ~= keepalive then
		misc.user_error("keepalive expected to be %08X, is actually %08X", keepalive, self.keepalive_)
	end
	if self.payload_ ~= payload then
		misc.user_error("payload expected to be %08X, is actually %08X", payload, self.payload_)
	end
	return self
end

function user_node_i:assert(keepalive, payload)
	return misc.user_wrap(function()
		return self:assert_(keepalive, payload)
	end)
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

local function make_node(typev)
	return setmetatable({
		type_            = typev,
		never_zero_      = false,
		marked_zeroable_ = false,
		select_group_    = false,
		output_count_    = 1,
	}, user_node_m)
end

local function default_label()
	local frame_name = misc.user_frame_name()
	return ("@%s"):format(frame_name)
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
	return node
end

local function make_constant(keepalive, payload)
	return misc.user_wrap(function()
		return make_constant_(keepalive, payload)
	end)
end

local function maybe_promote_number(thing)
	if type(thing) == "number" then
		thing = make_constant_(thing)
	end
	return thing
end

local function make_input(keepalive, payload)
	return misc.user_wrap(function()
		check_keepalive_payload(keepalive, payload)
		local node = make_node("input")
		node.keepalive_  = keepalive
		node.payload_    = payload
		node.terminal_   = true
		node.label_      = default_label()
		return node
	end)
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
		user_node_i[name] = function(...)
			return misc.user_wrap(func, ...)
		end
		opnames[name] = info
	end
end

add_op("band", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bitx.band(lhs.keepalive_, rhs.keepalive_)
		local payload = bitx.band(lhs.payload_, rhs.payload_)
		return keepalive, payload
	end,
	exec = function(lhs, rhs)
		return bitx.band(lhs, rhs)
	end,
	method = "filt_tmp",
	filt_tmp = 1,
	commutative = true,
})
add_op("bor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bitx.bor(lhs.keepalive_, rhs.keepalive_)
		local payload = bitx.band(bitx.bor(lhs.payload_, rhs.payload_), bitx.bxor(keepalive, check.payload_bits))
		return keepalive, payload
	end,
	exec = function(lhs, rhs)
		return bitx.bor(lhs, rhs)
	end,
	method = "filt_tmp",
	filt_tmp = 2,
	commutative = true,
})
add_op("bsub", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bitx.band(lhs.keepalive_, bitx.bxor(rhs.keepalive_, check.keepalive_bits))
		local payload = bitx.band(lhs.payload_, bitx.bxor(rhs.payload_, check.payload_bits))
		return keepalive, payload
	end,
	exec = function(lhs, rhs)
		return bitx.band(lhs, bitx.bxor(rhs, check.payload_bits))
	end,
	method = "filt_tmp",
	filt_tmp = 3,
	commutative = false,
})
add_op("bxor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bitx.bxor(lhs.keepalive_, rhs.keepalive_)
		local payload = bitx.bor(lhs.payload_, rhs.payload_)
		return keepalive, payload
	end,
	exec = function(lhs, rhs)
		return bitx.bxor(lhs, rhs)
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
local function do_shifts(lhs, rhs, func)
	local keepalive, payload
	for _, shift in ipairs(get_shifts(rhs)) do
		local i_keepalive = bitx.band(func(lhs.keepalive_, shift), check.shift_aware_bits)
		local i_payload = bitx.band(func(lhs.payload_, shift), check.shift_aware_bits)
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
	method = "filt_tmp",
	filt_tmp = 11,
	commutative = false,
})
add_op("select", {
	params = { "cond", "vnonzero", "vzero" },
	payload = function(cond, vnonzero, vzero)
		return one_of(vnonzero, vzero)
	end,
	method = "select",
})

return strict.make_mt_one("spaghetti.user_node", {
	maybe_promote_number_ = maybe_promote_number,
	make_constant         = make_constant,
	make_constant_        = make_constant_,
	make_input            = make_input,
	mt_                   = user_node_m,
	opnames_              = opnames,
})
