local strict = require("spaghetti.strict")
strict.wrap_env()

local misc = require("spaghetti.misc")

local function bfs(to_visit, visit)
	local seen = {}
	for node in pairs(to_visit) do
		seen[node] = true
	end
	while next(to_visit) do
		local next_to_visit = {}
		for node, edge in pairs(to_visit) do
			for next_node, next_edge in pairs(visit(node, edge)) do
				if not seen[next_node] then
					seen[next_node] = true
					next_to_visit[next_node] = next_edge
				end
			end
		end
		to_visit = next_to_visit
	end
	return seen
end

local function ts(initial, children, parents, visit) -- visit returns whether to enqueue children
	local parents_left = {}
	local ready = {}
	for node in pairs(initial) do
		parents_left[node] = misc.count_keys(parents[node])
		if parents_left[node] == 0 then
			ready[node] = true
		end
	end
	bfs(ready, function(node)
		local neighbours = {}
		if visit(node) then
			for child in pairs(children[node]) do
				if not parents_left[child] then
					parents_left[child] = misc.count_keys(parents[child])
				end
				parents_left[child] = parents_left[child] - 1
				if parents_left[child] == 0 then
					neighbours[child] = true
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
