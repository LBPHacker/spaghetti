#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <vector>

namespace
{
	struct CheckCin
	{
		int errAt;

		CheckCin(int newErrAt) : errAt(newErrAt)
		{
		}
	};
	std::istream &operator >>(std::istream &stream, const CheckCin &checkCin)
	{
		if (!stream)
		{
			std::cerr << "failed CheckCin at line " << checkCin.errAt << std::endl;
			exit(1);
		}
		return stream;
	}
#define CheckCin() CheckCin(__LINE__)

	static void checkRange(int32_t v, int32_t l, int32_t h, int errAt)
	{
		if (v < l || v >= h)
		{
			std::cerr << "failed checkRange at line " << errAt << std::endl;
			exit(1);
		}
	}
#define checkRange(v, l, h) checkRange((v), (l), (h), __LINE__)

	enum LinkDirection
	{
		linkUpstream,
		linkDownstream,
		linkMax,
	};

	struct Layer
	{
		std::vector<int32_t> nodeIndices;
		int32_t workSlotsUsed;
	};

	struct Link
	{
		enum LinkType
		{
			toBinary,
			toSelectNonzero,
			toSelectZero,
			toOutput,
		} type;
		struct Direction
		{
			int32_t nodeIndex = -1;
			int32_t linkIndicesIndex = -1;
		};
		std::array<Direction, linkMax> directions;
	};

	struct Node
	{
		enum
		{
			constant,
			input,
			binary,
			select,
			output,
		} type;
		std::array<std::vector<int32_t>, linkMax> linkIndices;
		std::vector<int32_t> tmps;
		int32_t workSlotsNeeded = -1;
	};

	struct Move
	{
		int32_t nodeIndex = -1;
		int32_t layerIndex2 = -1;
	};

	struct Tmp
	{
		bool commutative;
	};

	struct State;

	struct Tree
	{
		int32_t workSlots;
		int32_t constantCount;
		int32_t inputCount;
		int32_t compositeCount;
		int32_t outputCount;
		std::vector<Node> nodes;
		std::vector<Link> links;
		std::vector<Tmp> tmps;

		State Initial() const;

		struct CheckResult
		{
			int32_t workSlots;
		};
		std::optional<CheckResult> CheckLayer(const std::vector<int32_t> &nodeIndices) const
		{
			// we assume that node order between layers is correct
			// but we detect node order violations within the layer
			for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(nodeIndices.size()) - 1; ++nodeIndicesIndex)
			{
				auto &node = nodes[nodeIndices[nodeIndicesIndex]];
				if (node.type == Node::select)
				{
					// select somewhere other than at the end
					return std::nullopt;
				}
			}
			CheckResult checkResult;
			checkResult.workSlots = 0;
			auto nodeIndexInLayer = [&nodeIndices](int32_t nodeIndex) -> std::optional<int32_t> {
				auto it = std::find(nodeIndices.begin(), nodeIndices.end(), nodeIndex);
				if (it == nodeIndices.end())
				{
					return std::nullopt;
				}
				return int32_t(it - nodeIndices.begin());
			};
			for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(nodeIndices.size()); ++nodeIndicesIndex)
			{
				auto &node = nodes[nodeIndices[nodeIndicesIndex]];
				checkResult.workSlots += node.workSlotsNeeded;
				int32_t sameLayerBinaryLinkCount = 0;
				for (auto linkIndex : node.linkIndices[linkDownstream])
				{
					auto &link = links[linkIndex];
					auto linkedNodeIndex = link.directions[linkDownstream].nodeIndex;
					auto &linkedNode = nodes[linkedNodeIndex];
					auto linkedNodeIndexInLayer = nodeIndexInLayer(linkedNodeIndex);
					if (linkedNodeIndexInLayer)
					{
						if (link.type == Link::toBinary)
						{
							if (*linkedNodeIndexInLayer != nodeIndicesIndex + 1)
							{
								// binary same-layer link with non-adjacent node
								return std::nullopt;
							}
							if (link.directions[linkDownstream].linkIndicesIndex == 1 && !tmps[linkedNode.tmps[0]].commutative)
							{
								// binary same-layer link to lhs of non-commutative node
								return std::nullopt;
							}
							sameLayerBinaryLinkCount += 1;
							if (sameLayerBinaryLinkCount > 1)
							{
								// multiple binary same-layer links
								return std::nullopt;
							}
						}
						if (link.type == Link::toSelectNonzero)
						{
							// nonzero same-layer link
							return std::nullopt;
						}
						if (link.type == Link::toBinary || link.type == Link::toSelectZero)
						{
							// this saves a load
							checkResult.workSlots -= 1;
						}
					}
				}
			}
			if (checkResult.workSlots <= workSlots)
			{
				return checkResult;
			}
			// needs too many work slots
			return std::nullopt;
		}
	};

	struct State
	{
		const Tree *tree{};
		std::vector<int32_t> nodeIndices;
		std::vector<int32_t> layers;

		int32_t LayerSize(int32_t layerIndex) const
		{
			return LayerBegins(layerIndex + 1) - LayerBegins(layerIndex);
		}

		std::vector<int32_t> InsertNode(int32_t layerIndex, int32_t extraNodeIndex) const
		{
			// we assume that inserting the node into this layer doesn't violate order
			// we only have to figure out where within the layer it should be inserted
			auto layerBegin = LayerBegins(layerIndex);
			auto layerEnd = LayerBegins(layerIndex + 1);
			auto &extraNode = tree->nodes[extraNodeIndex];
			auto nodeIndicesCopy = std::vector(nodeIndices.begin() + layerBegin, nodeIndices.begin() + layerEnd);
			// insert up front by default, or at the back if it's a select
			int32_t insertAt = extraNode.type == Node::select ? nodeIndicesCopy.size() : 0;
			for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
			{
				auto nodeIndex = nodeIndices[nodeIndicesIndex];
				auto &node = tree->nodes[nodeIndex];
				for (auto dir = LinkDirection(0); dir < linkMax; dir = LinkDirection(int32_t(dir) + 1))
				{
					for (auto linkIndex : node.linkIndices[dir])
					{
						auto &link = tree->links[linkIndex];
						if (link.type == Link::toBinary && link.directions[dir].nodeIndex == extraNodeIndex)
						{
							// due to the order assumption above, this runs in only one of the dir iterations
							// not necessarily in only one of the linkIndex iterations, but that problem is handled elsewhere
							insertAt = (dir == linkUpstream ? nodeIndicesIndex : (nodeIndicesIndex + 1)) - layerBegin;
						}
					}
				}
			}
			nodeIndicesCopy.insert(nodeIndicesCopy.begin() + insertAt, extraNodeIndex);
			return nodeIndicesCopy;
		}

		std::vector<int32_t> NodeIndexToLayerIndex() const
		{
			std::vector<int32_t> nodeIndexToLayerIndex(tree->nodes.size());
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

		std::vector<Move> ValidMoves() const
		{
			auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
			std::vector<Move> moves;
			for (int32_t compositeIndex = 0; compositeIndex < tree->compositeCount; ++compositeIndex)
			{
				auto nodeIndex = tree->constantCount + tree->inputCount + compositeIndex;
				auto &node = tree->nodes[nodeIndex];
				auto currLayerIndex = nodeIndexToLayerIndex[nodeIndex];
				// move it somewhere between before the first and after the last composite layers
				std::array<int32_t, linkMax> newLayerIndex2Limit = {{ 1, int32_t(layers.size()) * 2 - 3 }};
				// don't move it to the same layer
				std::array<int32_t, linkMax> newLayerIndex2Skip = {{ currLayerIndex * 2, currLayerIndex * 2 }};
				for (auto dir = LinkDirection(0); dir < linkMax; dir = LinkDirection(int32_t(dir) + 1))
				{
					auto sign = dir == linkUpstream ? 1 : -1;
					for (auto linkIndex : node.linkIndices[dir])
					{
						auto &link = tree->links[linkIndex];
						auto linkedNodeIndex = link.directions[dir].nodeIndex;
						// don't move to layers that are beyond the closest neighbouring nodes
						newLayerIndex2Limit[dir] = sign * std::max(sign * newLayerIndex2Limit[dir], sign * nodeIndexToLayerIndex[linkedNodeIndex] * 2);
					}
					if (LayerSize(currLayerIndex) == 1)
					{
						// don't move it before or after the same layer either if that layer would just disappear
						newLayerIndex2Skip[dir] -= sign;
					}
				}
				for (int32_t newLayerIndex2 = newLayerIndex2Limit[linkUpstream]; newLayerIndex2 <= newLayerIndex2Limit[linkDownstream]; ++newLayerIndex2)
				{
					if (newLayerIndex2 >= newLayerIndex2Skip[linkUpstream] && newLayerIndex2 <= newLayerIndex2Skip[linkDownstream])
					{
						continue;
					}
					// make sure we can move it to an existing layer
					if (!(newLayerIndex2 & 1) && !bool(tree->CheckLayer(InsertNode(int32_t(newLayerIndex2 / 2), nodeIndex))))
					{
						continue;
					}
					moves.push_back({ nodeIndex, newLayerIndex2 });
				}
			}
			return moves;
		}

		int32_t LayerBegins(int32_t layerIndex) const
		{
			if (layerIndex == int32_t(layers.size()))
			{
				return nodeIndices.size();
			}
			return layers[layerIndex];
		}

		State RandomNeighbour(std::mt19937_64 &rng) const
		{
			auto moves = ValidMoves();
			if (!moves.size())
			{
				return *this;
			}
			auto move = moves[rng() % moves.size()];
			State neighbour;
			neighbour.tree = tree;
			auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
			for (int32_t layerIndex2 = 0; layerIndex2 < int32_t(layers.size()) * 2; ++layerIndex2)
			{
				if (layerIndex2 & 1)
				{
					if (layerIndex2 == move.layerIndex2)
					{
						neighbour.layers.push_back(int32_t(neighbour.nodeIndices.size()));
						neighbour.nodeIndices.push_back(move.nodeIndex);
					}
				}
				else
				{
					auto layerIndex = int32_t(layerIndex2 / 2);
					auto layerBegin = LayerBegins(layerIndex);
					auto layerEnd = LayerBegins(layerIndex + 1);
					if (nodeIndexToLayerIndex[move.nodeIndex] == layerIndex)
					{
						if (LayerSize(layerIndex) > 1)
						{
							neighbour.layers.push_back(int32_t(neighbour.nodeIndices.size()));
							for (auto nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
							{
								auto nodeIndex = nodeIndices[nodeIndicesIndex];
								if (nodeIndex != move.nodeIndex)
								{
									neighbour.nodeIndices.push_back(nodeIndex);
								}
							}
						}
					}
					else
					{
						neighbour.layers.push_back(int32_t(neighbour.nodeIndices.size()));
						if (layerIndex2 == move.layerIndex2)
						{
							auto nodeIndicesCopy = InsertNode(layerIndex, move.nodeIndex);
							neighbour.nodeIndices.insert(neighbour.nodeIndices.end(), nodeIndicesCopy.begin(), nodeIndicesCopy.end());
						}
						else
						{
							neighbour.nodeIndices.insert(neighbour.nodeIndices.end(), nodeIndices.begin() + layerBegin, nodeIndices.begin() + layerEnd);
						}
					}
				}
			}
			assert(neighbour.nodeIndices.size() == tree->nodes.size());
			return neighbour;
		}

		double Energy() const
		{
			// TODO
			return 0.0;
		}
	};

	State Tree::Initial() const
	{
		State state;
		state.tree = this;
		for (int32_t nodeIndex = 0; nodeIndex < int32_t(nodes.size()); ++nodeIndex)
		{
			state.nodeIndices.push_back(nodeIndex);
		}
		state.layers.push_back(0);
		for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
		{
			state.layers.push_back(constantCount + inputCount + compositeIndex);
		}
		state.layers.push_back(constantCount + inputCount + compositeCount);
		return state;
	}

	double TransitionProbability(double energy, double newEnergy, double temp)
	{
		// TODO
		return 1.0;
	}

	std::istream &operator >>(std::istream &stream, Tree &tree)
	{
		constexpr int32_t bigNumber = 10000;
		std::ios::sync_with_stdio(false);
		int32_t tmpCount;
		stream >> tmpCount >> tree.workSlots >> CheckCin();
		checkRange(tmpCount, 1, bigNumber);
		checkRange(tree.workSlots, 2, bigNumber);
		tree.tmps.resize(tmpCount);
		for (auto &tmp : tree.tmps)
		{
			stream >> tmp.commutative >> CheckCin();
		}
		stream >> tree.constantCount >> tree.inputCount >> tree.compositeCount >> tree.outputCount >> CheckCin();
		checkRange(tree.constantCount, 0, bigNumber);
		checkRange(tree.inputCount, 1, bigNumber);
		checkRange(tree.compositeCount, 1, bigNumber);
		checkRange(tree.outputCount, 1, bigNumber);
		tree.nodes.resize(tree.constantCount + tree.inputCount + tree.compositeCount + tree.outputCount);
		auto tmpSelect = tmpCount;
		auto maxInnerTmp = tmpCount;
		auto maxOuterTmp = tmpCount + 1;
		auto maxSource = tree.constantCount + tree.inputCount;
		for (int32_t constantIndex = 0; constantIndex < tree.constantCount; ++constantIndex)
		{
			auto nodeIndex = constantIndex;
			auto &constant = tree.nodes[nodeIndex];
			constant.type = Node::constant;
		}
		for (int32_t inputIndex = 0; inputIndex < tree.inputCount; ++inputIndex)
		{
			auto nodeIndex = tree.constantCount + inputIndex;
			auto &input = tree.nodes[nodeIndex];
			input.type = Node::input;
		}
		auto link = [&tree](Node &node, uint32_t nodeIndex, Link::LinkType linkType) {
			auto &linkedNode = tree.nodes[nodeIndex];
			auto linkIndex = tree.links.size();
			Link &link = tree.links.emplace_back();
			link.type = linkType;
			link.directions[linkUpstream].nodeIndex = nodeIndex;
			link.directions[linkDownstream].nodeIndex = &node - &tree.nodes[0];
			link.directions[linkUpstream].linkIndicesIndex = linkedNode.linkIndices[linkDownstream].size();
			link.directions[linkDownstream].linkIndicesIndex = node.linkIndices[linkUpstream].size();
			linkedNode.linkIndices[linkDownstream].push_back(linkIndex);
			node.linkIndices[linkUpstream].push_back(linkIndex);
		};
		for (int32_t compositeIndex = 0; compositeIndex < tree.compositeCount; ++compositeIndex)
		{
			auto nodeIndex = tree.constantCount + tree.inputCount + compositeIndex;
			auto &node = tree.nodes[nodeIndex];
			int32_t tmp;
			stream >> tmp >> CheckCin();
			checkRange(tmpCount, 0, maxOuterTmp);
			if (tmp == tmpSelect)
			{
				node.type = Node::select;
				int32_t laneCount, stageCount;
				stream >> laneCount >> stageCount >> CheckCin();
				checkRange(laneCount, 1, bigNumber);
				checkRange(stageCount, 2, bigNumber);
				node.workSlotsNeeded = stageCount + laneCount * 2;
				checkRange(node.workSlotsNeeded, 1, tree.workSlots + 1);
				node.tmps.resize(stageCount - 1);
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					int32_t nonzeroSource, zeroSource;
					stream >> nonzeroSource >> zeroSource >> CheckCin();
					checkRange(nonzeroSource, 0, maxSource);
					checkRange(zeroSource, 0, maxSource);
					link(node, nonzeroSource, Link::toSelectNonzero);
					link(node, zeroSource, Link::toSelectZero);
				}
				for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
				{
					if (stageIndex > 0)
					{
						int32_t tmp;
						stream >> tmp >> CheckCin();
						checkRange(tmp, 0, maxInnerTmp);
						node.tmps[stageIndex - 1] = tmp;
					}
					int32_t source;
					stream >> source >> CheckCin();
					checkRange(source, 0, maxSource);
					link(node, source, Link::toBinary);
				}
				maxSource += laneCount;
			}
			else
			{
				node.type = Node::binary;
				node.tmps.resize(1);
				node.tmps[0] = tmp;
				int32_t rhsSource, lhsSource;
				stream >> rhsSource >> lhsSource >> CheckCin();
				checkRange(rhsSource, 0, maxSource);
				checkRange(lhsSource, 0, maxSource);
				link(node, rhsSource, Link::toBinary);
				link(node, lhsSource, Link::toBinary);
				node.workSlotsNeeded = 2;
				maxSource += 1;
			}
		}
		for (int32_t outputIndex = 0; outputIndex < tree.outputCount; ++outputIndex)
		{
			auto nodeIndex = tree.constantCount + tree.inputCount + tree.compositeCount + outputIndex;
			auto &node = tree.nodes[nodeIndex];
			int32_t outputSource;
			stream >> outputSource >> CheckCin();
			checkRange(outputSource, 0, maxSource);
			node.type = Node::output;
			link(node, outputSource, Link::toOutput);
		}
		return stream;
	}

	std::ostream &operator <<(std::ostream &stream, const State &state)
	{
		stream << "============= BEGIN STATE =============" << std::endl;
		for (int32_t layerIndex = 1; layerIndex < int32_t(state.layers.size()) - 1; ++layerIndex)
		{
			auto layerBegin = state.LayerBegins(layerIndex);
			auto layerEnd = state.LayerBegins(layerIndex + 1);
			for (auto nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
			{
				auto nodeIndex = state.nodeIndices[nodeIndicesIndex];
				stream << std::setw(3) << nodeIndex << " ";
			}
			stream << std::endl;
		}
		stream << "============== END STATE ==============" << std::endl;
		return stream;
	}
}

int main()
{
	constexpr auto tempInit = 1.0;
	constexpr auto tempFini = 1e-7;
	constexpr auto tempLoss = 0.99999;
	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_real_distribution<double> rdist(0.0, 1.0);
	Tree tree;
	std::cin >> tree;
	State state = tree.Initial();
	std::cerr << state;
	auto temp = tempInit;
	while (temp > tempFini)
	{
		auto newState = state.RandomNeighbour(rng);
		if (TransitionProbability(state.Energy(), newState.Energy(), temp) >= rdist(rng))
		{
			state = std::move(newState);
			std::cerr << state;
		}
		temp *= tempLoss;
	}
	return 0;
}