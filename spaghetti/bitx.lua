assert(bit, "no bit API, are you running this under luajit?")

local bitx = {}

local function wrap32(name)
	local func = bit[name]
	bitx[name] = function(...)
		return func(...) % 0x100000000
	end
end
wrap32("band")
wrap32("bor")
wrap32("bxor")
wrap32("rshift")
wrap32("lshift")

return bitx
