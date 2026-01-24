#include "State.hpp"
#include "Design.hpp"
#include <cassert>

namespace Spaghetti::Optimize
{
	int32_t State::LayerSize(int32_t layerIndex) const
	{
		return LayerBegins(layerIndex + 1) - LayerBegins(layerIndex);
	}

	std::vector<int32_t> State::InsertNode(int32_t layerIndex, int32_t extraNodeIndex) const
	{
		// we assume that inserting the node into this layer doesn't violate order
		// we only have to figure out where within the layer it should be inserted
		auto layerBegin = LayerBegins(layerIndex);
		auto layerEnd = LayerBegins(layerIndex + 1);
		auto &extraNode = design->nodes[extraNodeIndex];
		auto nodeIndicesCopy = std::vector(nodeIndices.begin() + layerBegin, nodeIndices.begin() + layerEnd);
		// insert up front by default, or at the back if it's a select
		int32_t insertAt = extraNode.type == Node::select ? nodeIndicesCopy.size() : 0;
		for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
		{
			auto nodeIndex = nodeIndices[nodeIndicesIndex];
			auto &node = design->nodes[nodeIndex];
			for (auto dir = Link::Direction(0); dir < Link::directionMax; dir = Link::Direction(int32_t(dir) + 1))
			{
				for (auto linkIndex : node.linkIndices[dir])
				{
					auto &link = design->links[linkIndex];
					if (link.type == Link::toBinary && link.neighbours[dir].nodeIndex == extraNodeIndex)
					{
						// due to the order assumption above, this runs in only one of the dir iterations
						// not necessarily in only one of the linkIndex iterations, but that problem is handled elsewhere
						insertAt = (dir == Link::directionUp ? nodeIndicesIndex : (nodeIndicesIndex + 1)) - layerBegin;
					}
				}
			}
		}
		nodeIndicesCopy.insert(nodeIndicesCopy.begin() + insertAt, extraNodeIndex);
		return nodeIndicesCopy;
	}

	std::vector<int32_t> State::NodeIndexToLayerIndex() const
	{
		std::vector<int32_t> nodeIndexToLayerIndex(design->nodes.size());
		for (int32_t layerIndex = 0; layerIndex < int32_t(layers.size()); ++layerIndex)
		{
			auto layerBegin = LayerBegins(layerIndex);
			auto layerEnd = LayerBegins(layerIndex + 1);
			for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
			{
				nodeIndexToLayerIndex[nodeIndices[nodeIndicesIndex]] = layerIndex;
			}
		}
		return nodeIndexToLayerIndex;
	}

	std::vector<State::Move> State::PossibleMoves() const
	{
		auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
		std::vector<Move> moves;
		for (int32_t compositeIndex = 0; compositeIndex < design->compositeCount; ++compositeIndex)
		{
			auto nodeIndex = design->constantCount + design->inputCount + compositeIndex;
			auto &node = design->nodes[nodeIndex];
			auto currLayerIndex = nodeIndexToLayerIndex[nodeIndex];
			// move it somewhere between before the first and after the last composite layers
			std::array<int32_t, Link::directionMax> newLayerIndex2Limit = {{ 1, int32_t(layers.size()) * 2 - 3 }};
			// don't move it to the same layer
			std::array<int32_t, Link::directionMax> newLayerIndex2Skip = {{ currLayerIndex * 2, currLayerIndex * 2 }};
			for (auto dir = Link::Direction(0); dir < Link::directionMax; dir = Link::Direction(int32_t(dir) + 1))
			{
				auto sign = dir == Link::directionUp ? 1 : -1;
				for (auto linkIndex : node.linkIndices[dir])
				{
					auto &link = design->links[linkIndex];
					auto linkedNodeIndex = link.neighbours[dir].nodeIndex;
					// don't move to layers that are beyond the closest neighbouring nodes
					newLayerIndex2Limit[dir] = sign * std::max(sign * newLayerIndex2Limit[dir], sign * nodeIndexToLayerIndex[linkedNodeIndex] * 2);
				}
				if (LayerSize(currLayerIndex) == 1)
				{
					// don't move it before or after the same layer either if that layer would just disappear
					newLayerIndex2Skip[dir] -= sign;
				}
			}
			for (int32_t newLayerIndex2 = newLayerIndex2Limit[Link::directionUp]; newLayerIndex2 <= newLayerIndex2Limit[Link::directionDown]; ++newLayerIndex2)
			{
				if (newLayerIndex2 >= newLayerIndex2Skip[Link::directionUp] && newLayerIndex2 <= newLayerIndex2Skip[Link::directionDown])
				{
					continue;
				}
				moves.push_back({ nodeIndex, newLayerIndex2 });
			}
		}
		return moves;
	}

	int32_t State::LayerBegins(int32_t layerIndex) const
	{
		if (layerIndex == int32_t(layers.size()))
		{
			return nodeIndices.size();
		}
		return layers[layerIndex];
	}

	std::optional<State::Move> State::RandomValidMove(std::mt19937_64 &rng) const
	{
		auto moves = PossibleMoves();
		while (true)
		{
			if (!moves.size())
			{
				break;
			}
			auto index = rng() % moves.size();
			auto &move = moves[index];
			// make sure we can move it to an existing layer
			if (move.layerIndex2 & 1)
			{
				return move;
			}
			auto withNewNode = InsertNode(int32_t(move.layerIndex2 / 2), move.nodeIndex);
			if (design->CheckLayer(withNewNode.begin(), withNewNode.end()))
			{
				return move;
			}
			std::swap(moves.back(), moves[index]);
			moves.resize(moves.size() - 1U);
		}
		return std::nullopt;
	}

	std::shared_ptr<State> State::RandomNeighbour(std::mt19937_64 &rng) const
	{
		auto move = RandomValidMove(rng);
		if (!move)
		{
			return std::make_shared<State>(*this);
		}
		auto neighbour = std::make_shared<State>();
		neighbour->iteration = iteration + 1;
		neighbour->design = design;
		auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
		for (int32_t layerIndex2 = 0; layerIndex2 < int32_t(layers.size()) * 2; ++layerIndex2)
		{
			if (layerIndex2 & 1)
			{
				if (layerIndex2 == move->layerIndex2)
				{
					neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
					neighbour->nodeIndices.push_back(move->nodeIndex);
				}
			}
			else
			{
				auto layerIndex = int32_t(layerIndex2 / 2);
				auto layerBegin = LayerBegins(layerIndex);
				auto layerEnd = LayerBegins(layerIndex + 1);
				if (nodeIndexToLayerIndex[move->nodeIndex] == layerIndex)
				{
					if (LayerSize(layerIndex) > 1)
					{
						neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
						for (auto nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
						{
							auto nodeIndex = nodeIndices[nodeIndicesIndex];
							if (nodeIndex != move->nodeIndex)
							{
								neighbour->nodeIndices.push_back(nodeIndex);
							}
						}
					}
				}
				else
				{
					neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
					if (layerIndex2 == move->layerIndex2)
					{
						auto nodeIndicesCopy = InsertNode(layerIndex, move->nodeIndex);
						neighbour->nodeIndices.insert(neighbour->nodeIndices.end(), nodeIndicesCopy.begin(), nodeIndicesCopy.end());
					}
					else
					{
						neighbour->nodeIndices.insert(neighbour->nodeIndices.end(), nodeIndices.begin() + layerBegin, nodeIndices.begin() + layerEnd);
					}
				}
			}
		}
		assert(neighbour->nodeIndices.size() == design->nodes.size());
		return neighbour;
	}
}
