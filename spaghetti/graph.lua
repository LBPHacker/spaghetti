local strict = require("spaghetti.strict")
strict.wrap_env()

local priority_queue = require("spaghetti.priority_queue")
local misc           = require("spaghetti.misc")

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

local function astar(initial, final, neighbours, heuristic)
	local meta = {}
	local inserted = 0
	local function discover(node)
		inserted = inserted + 1
		meta[node] = {
			cumulative = math.huge,
			inserted   = inserted,
			neighbours = neighbours(node),
			heuristic  = heuristic(node),
		}
	end
	local pq = priority_queue.make_priority_queue(function(left, right)
		local leftm = meta[left]
		local rightm = meta[right]
		if leftm.perceived ~= rightm.perceived then
			return leftm.perceived < rightm.perceived
		end
		return leftm.inserted > rightm.inserted
	end)
	discover(initial)
	meta[initial].cumulative = 0
	meta[initial].perceived = meta[initial].heuristic
	pq:insert(initial)
	while not pq:empty() do
		local current = pq:remove()
		if current == final then
			local path = {}
			while current do
				table.insert(path, current)
				current = meta[current].previous
			end
			return misc.reverse(path)
		end
		local current_cumulative = meta[current].cumulative
		for neighbour, distance in pairs(meta[current].neighbours) do
			local neighbour_cumulative = current_cumulative + distance
			if not meta[neighbour] then
				discover(neighbour)
			end
			local neighbourm = meta[neighbour]
			if neighbour_cumulative < neighbourm.cumulative then
				neighbourm.previous = current
				neighbourm.cumulative = neighbour_cumulative
				neighbourm.perceived = neighbour_cumulative + neighbourm.heuristic
				pq:upsert(neighbour)
			end
		end
	end
end

return {
	bfs   = bfs,
	ts    = ts,
	astar = astar,
}
