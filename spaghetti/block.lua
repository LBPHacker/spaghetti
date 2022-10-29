local check = require("check")

local block_i = {}
local block_m = { type = "block", __index = block_i }



-- function block_i:bake()
-- 	local level = 2
	
-- end

-- local function particle(properties)
-- 	local part = setmetatable({
-- 		properties = {},
-- 	}, block_m)
-- 	return part
-- end


-- local function particle(properties)
-- 	local part = block()
	
-- 	return part
-- end

local function resource(func)
end

local function block(func)
end

local property_defaults = {}
local particle = block(function(self, level, design, data)
	level = level + 1
	self.type = resource(check.part_type("data.type", level, data.type))
	local defaults = property_defaults[self.type] or {}
	self.life    = resource(check.part_life   ("data.life"   , level, data.life    or defaults.life   ))
	self.ctype   = resource(check.part_ctype  ("data.ctype"  , level, data.ctype   or defaults.ctype  ))
	self.x       = resource(check.part_x      ("data.x"      , level, data.x       or defaults.x      ))
	self.y       = resource(check.part_y      ("data.y"      , level, data.y       or defaults.y      ))
	self.z       = resource()
	self.vx      = resource(check.part_vx     ("data.vx"     , level, data.vx      or defaults.vx     ))
	self.vy      = resource(check.part_vy     ("data.vy"     , level, data.vy      or defaults.vy     ))
	self.temp    = resource(check.part_temp   ("data.temp"   , level, data.temp    or defaults.temp   ))
	self.tmp     = resource(check.part_tmp    ("data.tmp"    , level, data.tmp     or defaults.tmp    ))
	self.tmp2    = resource(check.part_tmp2   ("data.tmp2"   , level, data.tmp2    or defaults.tmp2   ))
	self.tmp3    = resource(check.part_tmp3   ("data.tmp3"   , level, data.tmp3    or defaults.tmp3   ))
	self.tmp4    = resource(check.part_tmp4   ("data.tmp4"   , level, data.tmp4    or defaults.tmp4   ))
	self.dcolour = resource(check.part_dcolour("data.dcolour", level, data.dcolour or defaults.dcolour))
	if self.above then
		self.z:set(self.above.z:get())
	else
		self.z:set(0)
	end
	design:emit({

	})
end)

return {
	resource = resource,
	block = block,
	particle = particle,
}
