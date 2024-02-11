local strict = require("spaghetti.strict")
strict.wrap_env()

local check = require("spaghetti.check")
local misc  = require("spaghetti.misc")

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
	misc.user_error(message:format(("input %s of %s connected to %s"):format(name, tostring(self), tostring(self.params_[name]))))
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
			if self.params_.lhs.marked_zeroable_ and self.params_.rhs.marked_zeroable_ then
				misc.user_error("both inputs of %s are marked zeroable", tostring(self))
			end
			-- if not self.info_.commutative, rhs must not be the zeroable input
			if not self.info_.commutative and self.params_.rhs.marked_zeroable_ then
				self:input_error_("rhs", "%s is marked zeroable")
			end
		else
			-- no input may be marked zeroable
			if self.params_.lhs.marked_zeroable_ then
				self:input_error_("lhs", "%s is marked zeroable")
			end
			if self.params_.rhs.marked_zeroable_ then
				self:input_error_("rhs", "%s is marked zeroable")
			end
		end
	elseif self.info_.method == "select" then
		if self.marked_zeroable_ then
			misc.user_error("%s marked zeroable despite being a select", tostring(self))
		end
		if not self.params_.cond.marked_zeroable_ then
			self:input_error_("cond", "%s is not marked zeroable")
		end
		if self.params_.vnonzero.marked_zeroable_ then
			self:input_error_("vnonzero", "%s is marked zeroable")
		end
		if self.params_.vzero.marked_zeroable_ then
			self:input_error_("vzero", "%s is marked zeroable")
		end
	-- elseif self.info_.method == "fallback" then
	-- 	if not self.params_.cond.marked_zeroable_ then
	-- 		self:input_error_("cond", "%s is not marked zeroable")
	-- 	end
	-- 	if self.params_.vzero.marked_zeroable_ then
	-- 		self:input_error_("vzero", "%s is marked zeroable")
	-- 	end
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
	if bit.band(keepalive, payload) ~= 0 then
		misc.user_error("keepalive and payload share bits")
	end
	if bit.bor(keepalive, payload) == 0 then
		misc.user_error("keepalive and payload are empty")
	end
end

local function make_node(typev)
	return setmetatable({
		type_       = typev,
		never_zero_ = false,
		marked_zeroable_  = false,
	}, user_node_m)
end

local function default_label()
	local frame_name = misc.user_frame_name()
	return ("@%s"):format(frame_name)
end

local function make_constant_(keepalive, payload)
	if payload == nil then
		payload = 0
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
				node.params_[info.params[i]] = params[i]
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
		local keepalive = bit.band(lhs.keepalive_, rhs.keepalive_)
		local payload = bit.band(lhs.payload_, rhs.payload_)
		return keepalive, payload
	end,
	method = "filt_tmp",
	filt_tmp = 1,
	commutative = true,
})
add_op("bor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bit.bor(lhs.keepalive_, rhs.keepalive_)
		local payload = bit.band(bit.bor(lhs.payload_, rhs.payload_), bit.bxor(keepalive, check.ctype_bits))
		return keepalive, payload
	end,
	method = "filt_tmp",
	filt_tmp = 2,
	commutative = true,
})
add_op("bsub", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bit.band(lhs.keepalive_, bit.bxor(rhs.keepalive_, check.ctype_bits))
		local payload = bit.band(lhs.payload_, bit.bxor(rhs.payload_, check.ctype_bits))
		return keepalive, payload
	end,
	method = "filt_tmp",
	filt_tmp = 3,
	commutative = false,
})
add_op("bxor", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		local keepalive = bit.bxor(lhs.keepalive_, rhs.keepalive_)
		local payload = bit.bor(lhs.payload_, rhs.payload_)
		return keepalive, payload
	end,
	method = "filt_tmp",
	filt_tmp = 7,
	commutative = true,
})
local function one_of(lhs, rhs)
	local keepalive = bit.band(lhs.keepalive_, rhs.keepalive_)
	local payload = bit.bor(lhs.payload_, rhs.payload_, bit.bxor(lhs.keepalive_, rhs.keepalive_))
	return keepalive, payload
end
local function get_shifts(rhs)
	local last = 29
	local shifts = {}
	for i = 0, 29 do
		if bit.band(bit.lshift(1, i), rhs.keepalive_) ~= 0 then
			last = i
			table.insert(shifts, i)
			break
		end
	end
	for i = 0, last - 1 do
		if bit.band(bit.lshift(1, i), rhs.payload_) ~= 0 then
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
		local i_keepalive = bit.band(func(lhs.keepalive_, shift), check.ctype_bits)
		local i_payload = bit.band(func(lhs.payload_, shift), check.ctype_bits)
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
		return do_shifts(lhs, rhs, bit.lshift)
	end,
	method = "filt_tmp",
	filt_tmp = 10,
	commutative = false,
})
add_op("rshift", {
	params = { "lhs", "rhs" },
	payload = function(lhs, rhs)
		return do_shifts(lhs, rhs, bit.rshift)
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
-- add_op("fallback", {
-- 	params = { "cond", "vzero" },
-- 	payload = function(cond, vzero)
-- 		return one_of(cond, vzero)
-- 	end,
-- 	method = "fallback",
-- })

return strict.make_mt_one("spaghetti.user_node", {
	maybe_promote_number_ = maybe_promote_number,
	make_constant         = make_constant,
	make_input            = make_input,
	mt_                   = user_node_m,
	opnames_              = opnames,
})
