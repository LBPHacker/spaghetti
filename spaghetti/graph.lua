local strict = require("spaghetti.strict")
strict.wrap_env()

local misc        = require("spaghetti.misc")
local ordered_map = require("spaghetti.ordered_map")

local function bfs(to_visit, visit)
	local seen = {}
	for node in to_visit:ipairs() do
		seen[node] = true
	end
	while to_visit:count() > 0 do
		local next_to_visit = ordered_map.make_ordered_map()
		for node, edge in to_visit:ipairs() do
			for next_node, next_edge in visit(node, edge):ipairs() do
				if not seen[next_node] then
					seen[next_node] = true
					next_to_visit:add(next_node, next_edge)
				end
			end
		end
		to_visit = next_to_visit
	end
	return seen
end

local function ts(initial, children, parents, visit) -- visit returns whether to enqueue children
	local parents_left = {}
	local ready = ordered_map.make_ordered_map()
	for node in initial:ipairs() do
		parents_left[node] = parents[node]:count()
		if parents_left[node] == 0 then
			ready:add(node)
		end
	end
	bfs(ready, function(node)
		local neighbours = ordered_map.make_ordered_map()
		if visit(node) then
			for child in children[node]:ipairs() do
				if not parents_left[child] then
					parents_left[child] = parents[child]:count()
				end
				parents_left[child] = parents_left[child] - 1
				if parents_left[child] == 0 then
					neighbours:add(child)
				end
			end
		end
		return neighbours
	end)
end

return {
	bfs = bfs,
	ts  = ts,
}
