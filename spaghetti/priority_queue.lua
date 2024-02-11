local strict = require("spaghetti.strict")
strict.wrap_env()

local priority_queue_m, priority_queue_i = strict.make_mt("spaghetti.priority_queue.priority_queue")

function priority_queue_i:swap_(left_index, right_index)
	local left, right = self.items_[left_index], self.items_[right_index]
	self.items_[right_index], self.items_[left_index] = left, right
	self.indices_[right], self.indices_[left] = left_index, right_index
end

function priority_queue_i:less_(left_index, right_index)
	return self.less_func_(self.items_[left_index], self.items_[right_index])
end

function priority_queue_i:swap_if_less_(left_index, right_index)
	if self:less_(left_index, right_index) then
		self:swap_(left_index, right_index)
		return true
	end
end

function priority_queue_i:normalize_(index)
	while index > 1 do
		local up_index = math.floor(index / 2)
		if self:swap_if_less_(index, up_index) then
			index = up_index
		else
			break
		end
	end
	while index * 2 <= #self.items_ do
		local down_index_1 = index * 2
		local down_index_2 = down_index_1 < #self.items_ and (down_index_1 + 1)
		if down_index_2 and self:less_(down_index_2, down_index_1) then
			down_index_1, down_index_2 = down_index_2, down_index_1
		end
		if self:swap_if_less_(down_index_1, index) then
			index = down_index_1
		elseif down_index_2 and self:swap_if_less_(down_index_2, index) then
			index = down_index_2
		else
			break
		end
	end
end

function priority_queue_i:upsert(item)
	if self.indices_[item] then
		self:update(item)
	else
		self:insert(item)
	end
end

function priority_queue_i:update(item)
	local index = assert(self.indices_[item])
	self:normalize_(index)
end

function priority_queue_i:insert(item, priority)
	assert(not self.indices_[item])
	local index = #self.items_ + 1
	self.items_[index] = item
	self.indices_[item] = index
	self:normalize_(index)
end

function priority_queue_i:remove()
	assert(not self:empty())
	self:swap_(1, #self.items_)
	local item = self.items_[#self.items_]
	self.items_[#self.items_] = nil
	self.indices_[item] = nil
	self:normalize_(1)
	return item
end

function priority_queue_i:empty()
	return #self.items_ == 0
end

local function make_priority_queue(less_func)
	return setmetatable({
		items_     = {},
		indices_   = {},
		less_func_ = less_func,
	}, priority_queue_m)
end

return {
	make_priority_queue = make_priority_queue,
}
