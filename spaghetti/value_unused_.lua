local strict = require("spaghetti.strict")
strict.wrap_env()

local check = require("spaghetti.check")

local value_m, value_i = strict.make_mt("spaghetti.value.value")

local function bit_count(ctype)
	local count = 0
	local rbit = 1
	while bit.band(rbit, check.ctype_bits) ~= 0 do
		if bit.band(ctype, rbit) ~= 0 then
			count = count + 1
		end
		rbit = bit.lshift(rbit, 1)
	end
	return count
end

function value_i:complete()
	return self.coverage_ == check.ctype_bits
end

function value_i:bor(other)
	local level = 2
	check.mt(level, value_m, "other", other)
end

local function make_value(known_bits, known_set_bits, unknown_bits, subvalue_bits, subvalues)
	local level = 2
	if subvalue_bits == nil and subvalues == nil then
		subvalue_bits = 0
		subvalues = {}
	end
	check.ctype(level, "known_bits", known_bits)
	check.ctype(level, "unknown_bits", unknown_bits)
	check.ctype(level, "subvalue_bits", subvalue_bits)
	if bit.band(known_bits, unknown_bits) ~= 0 then
		error("known_bits shares bits with unknown_bits", level)
	end
	if bit.band(subvalue_bits, unknown_bits) ~= 0 then
		error("subvalue_bits shares bits with unknown_bits", level)
	end
	if bit.band(subvalue_bits, known_bits) ~= 0 then
		error("subvalue_bits shares bits with known_bits", level)
	end
	check.ctype(level, "known_set_bits", known_set_bits)
	if bit.band(bit.bxor(known_bits, check.ctype_bits), known_set_bits) ~= 0 then
		error("known_set_bits is not a subet of known_bits", level)
	end
	check.table(level, "subvalues", subvalues)
	local subvalue_count = 0
	local subvalues_copy = {}
	local first_coverage, first_valuename
	for key, value in pairs(subvalues) do
		local keyname = "subvalues key " .. tostring(key)
		check.ctype(level, keyname, key)
		if bit.band(bit.bxor(subvalue_bits, check.ctype_bits), key) ~= 0 then
			error(keyname .. " is not a subet of subvalue_bits", level)
		end
		local valuename = "subvalues[" .. tostring(key) .. "]"
		check.mt(level, value_m, valuename, value)
		first_valuename = first_valuename or valuename
		first_coverage = first_coverage or value.coverage_
		if value.coverage_ ~= first_coverage then
			error(valuename .. " has different coverage from " .. first_valuename, level)
		end
		subvalue_count = subvalue_count + 1
		subvalues_copy[key] = value
	end
	if subvalue_count ~= bit.lshift(1, bit_count(bit.bxor(bit.bor(known_bits, unknown_bits), check.ctype_bits))) then
		error("subvalues is not exhaustive", level)
	end
	if bit.band(first_coverage, subvalue_bits) ~= 0 then
		error(first_valuename .. " shares bits with subvalue_bits", level)
	end
	if bit.band(first_coverage, unknown_bits) ~= 0 then
		error(first_valuename .. " shares bits with unknown_bits", level)
	end
	if bit.band(first_coverage, known_bits) ~= 0 then
		error(first_valuename .. " shares bits with known_bits", level)
	end
	local coverage = bit.bor(known_bits, unknown_bits, subvalue_bits, first_coverage)
	return setmetatable({
		known_bits_     = known_bits,
		known_set_bits_ = known_set_bits,
		unknown_bits_   = unknown_bits,
		subvalue_bits_  = subvalue_bits,
		subvalues_      = subvalues_copy,
	}, value_m)
end
