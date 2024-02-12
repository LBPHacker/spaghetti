#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <optional>
#include <random>
#include <variant>
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
		int32_t upstreamOutputIndex = -1;
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
		std::vector<int32_t> sources;
	};

	struct Source
	{
		int32_t nodeIndex;
		int32_t outputIndex;
		int32_t uses = 0;
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
		int32_t storageSlots;
		int32_t constantCount;
		int32_t inputCount;
		int32_t compositeCount;
		int32_t outputCount;
		std::vector<Node> nodes;
		std::vector<Link> links;
		std::vector<Tmp> tmps;
		std::vector<Source> sources;

		double storageSlotOverheadPenalty;
		int32_t commitCost;
		int32_t loadCost;
		int32_t cloadCost;
		int32_t modeCost;
		int32_t storeCost;
		int32_t cstoreCost;

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
							int32_t lhsIndex = 1;
							if (linkedNode.type == Node::select)
							{
								auto laneCount = int32_t(linkedNode.sources.size());
								lhsIndex += laneCount * 2;
							}
							if (link.directions[linkDownstream].linkIndicesIndex == lhsIndex && !tmps[linkedNode.tmps[0]].commutative)
							{
								// binary same-layer link to lhs of non-commutative node
								return std::nullopt;
							}
							if (link.directions[linkDownstream].linkIndicesIndex > lhsIndex)
							{
								// binary same-layer link to parameter of higher index than that of rhs or lhs
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
		int32_t iteration;
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
			neighbour.iteration = iteration + 1;
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

		struct Commit
		{
		};
		struct Load
		{
			int32_t nodeIndex;
			int32_t tmp;
			int32_t workSlot;
			int32_t storageSlot;
		};
		struct Cload
		{
			int32_t nodeIndex;
			int32_t tmp;
			int32_t workSlot;
			int32_t storageSlot;
		};
		struct Mode
		{
			int32_t workSlot;
			int32_t tmp;
		};
		struct Store
		{
			int32_t workSlot;
			int32_t storageSlot;
		};
		struct Cstore
		{
			int32_t workSlot;
			int32_t storageSlot;
		};
		struct AllocStorage
		{
			int32_t sourceIndex;
			int32_t storageSlot;
			int32_t uses;
		};
		struct UseStorage
		{
			int32_t storageSlot;
		};
		using PlanStep = std::variant<
			Commit,
			Load,
			Cload,
			Mode,
			Store,
			Cstore,
			AllocStorage,
			UseStorage
		>;
		struct Energy
		{
			double linear;
			int32_t storageSlotCount;
			int32_t partCount = 0;
		};
		struct Plan : public Energy
		{
			std::vector<PlanStep> steps;
		};
		template<class EnergyType>
		EnergyType GetEnergy() const
		{
			EnergyType energy;
			auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
			struct Storage
			{
				int32_t usesLeft;
				int32_t slotIndex;
			};
			std::vector<std::optional<int32_t>> slots;
			std::vector<Storage> storage(tree->sources.size(), { 0, -1 });
			auto allocStorage = [this, &energy, &storage, &slots](int32_t sourceIndex, bool forConstant) {
				std::optional<int32_t> freeSlotIndex;
				for (int32_t slotIndex = 0; slotIndex < int32_t(slots.size()); ++slotIndex)
				{
					if (!slots[slotIndex])
					{
						freeSlotIndex = slotIndex;
						break;
					}
				}
				if (!freeSlotIndex)
				{
					freeSlotIndex = slots.size();
					slots.emplace_back();
				}
				slots[*freeSlotIndex] = sourceIndex;
				auto uses = tree->sources[sourceIndex].uses;
				if (forConstant)
				{
					uses = -1; // constants have infinite uses
				}
				storage[sourceIndex] = { uses, *freeSlotIndex };
				if constexpr (std::is_same_v<EnergyType, Plan>)
				{
					energy.steps.push_back(AllocStorage{ sourceIndex, *freeSlotIndex, uses });
				}
				return *freeSlotIndex;
			};
			auto useStorage = [&energy, &storage, &slots](int32_t sourceIndex) {
				auto slotIndex = storage[sourceIndex].slotIndex;
				auto &usesLeft = storage[sourceIndex].usesLeft;
				if (usesLeft != -1)
				{
					assert(usesLeft > 0);
					usesLeft -= 1;
					if (!usesLeft)
					{
						slots[slotIndex] = std::nullopt;
					}
				}
				if constexpr (std::is_same_v<EnergyType, Plan>)
				{
					energy.steps.push_back(UseStorage{ slotIndex });
				}
				return slotIndex;
			};
			for (int32_t constantIndex = 0; constantIndex < tree->constantCount; ++constantIndex)
			{
				auto nodeIndex = constantIndex;
				auto &node = tree->nodes[nodeIndex];
				auto sourceIndex = node.sources[0];
				allocStorage(sourceIndex, true);
			}
			for (int32_t inputIndex = 0; inputIndex < tree->inputCount; ++inputIndex)
			{
				auto nodeIndex = tree->constantCount + inputIndex;
				auto &node = tree->nodes[nodeIndex];
				auto sourceIndex = node.sources[0];
				allocStorage(sourceIndex, false);
			}
			if constexpr (std::is_same_v<EnergyType, Plan>)
			{
				energy.steps.push_back(Commit{});
			}
			for (int32_t layerIndex = 1; layerIndex < int32_t(layers.size()) - 1; ++layerIndex)
			{
				struct StoreScheduleEntry
				{
					int32_t sourceIndex;
					int32_t workSlotIndex;
					std::optional<int32_t> cworkSlotIndex;
				};
				std::vector<StoreScheduleEntry> storeSchedule;
				auto doStore = [&storeSchedule](int32_t workSlotIndex, int32_t sourceIndex) {
					auto storeScheduleIndex = int32_t(storeSchedule.size());
					storeSchedule.push_back({ sourceIndex, workSlotIndex });
					return storeScheduleIndex;
				};
				auto doCstore = [&storeSchedule](int32_t workSlotIndex, int32_t storeScheduleIndex) {
					storeSchedule[storeScheduleIndex].cworkSlotIndex = workSlotIndex;
				};
				struct TmpLoad
				{
					bool used;
					std::vector<int32_t> slotUsed; // std::vector<bool> is stupid
				};
				std::vector<TmpLoad> tmpLoads(tree->tmps.size(), { false, std::vector<int32_t>(slots.size(), 0) });
				auto doLoad = [this, &energy, &useStorage, &tmpLoads](int32_t nodeIndex, int32_t workSlotIndex, int32_t sourceIndex, int32_t tmp) {
					auto storageSlotIndex = useStorage(sourceIndex);
					if (!tmpLoads[tmp].used)
					{
						energy.partCount += tree->modeCost;
						if constexpr (std::is_same_v<EnergyType, Plan>)
						{
							energy.steps.push_back(Mode{ workSlotIndex, tmp });
						}
						tmpLoads[tmp].used = true;
					}
					if (tmpLoads[tmp].slotUsed[storageSlotIndex])
					{
						energy.partCount += tree->cloadCost;
						if constexpr (std::is_same_v<EnergyType, Plan>)
						{
							energy.steps.push_back(Cload{ nodeIndex, tmp, workSlotIndex, storageSlotIndex });
						}
					}
					else
					{
						tmpLoads[tmp].slotUsed[storageSlotIndex] = 1;
						energy.partCount += tree->loadCost;
						if constexpr (std::is_same_v<EnergyType, Plan>)
						{
							energy.steps.push_back(Load{ nodeIndex, tmp, workSlotIndex, storageSlotIndex });
						}
					}
				};
				auto layerBegin = LayerBegins(layerIndex);
				auto layerEnd = LayerBegins(layerIndex + 1);
				int32_t workSlotsUsed = 0;
				std::vector<int32_t> selectStorageSlotSchedule;
				{
					auto &lastNode = tree->nodes[nodeIndices[layerEnd - 1]];
					if (lastNode.type == Node::select)
					{
						selectStorageSlotSchedule.resize(lastNode.sources.size(), -1);
					}
				}
				for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
				{
					auto nodeIndex = nodeIndices[nodeIndicesIndex];
					auto &node = tree->nodes[nodeIndex];
					auto toSelectZeroLinkToSourceIndex = [this](const Link &link) {
						auto &node = tree->nodes[link.directions[linkDownstream].nodeIndex];
						auto laneIndex = (link.directions[linkDownstream].linkIndicesIndex - 1) / 2;
						return std::pair<int32_t, int32_t>{ laneIndex, node.sources[laneIndex] };
					};
					auto doLinkUpstream = [
						this,
						nodeIndex,
						&energy,
						&selectStorageSlotSchedule,
						&nodeIndexToLayerIndex,
						&workSlotsUsed,
						&doLoad,
						&doStore,
						&toSelectZeroLinkToSourceIndex,
						layerIndex
					](int32_t linkIndicesIndex) {
						auto &node = tree->nodes[nodeIndex];
						auto linkIndex = node.linkIndices[linkUpstream][linkIndicesIndex];
						auto &link = tree->links[linkIndex];
						auto linkedNodeIndex = link.directions[linkUpstream].nodeIndex;
						auto &linkedNode = tree->nodes[linkedNodeIndex];
						if (nodeIndexToLayerIndex[linkedNodeIndex] != layerIndex)
						{
							auto loadTmp = 0;
							auto stageIndex = linkIndicesIndex;
							if (node.type == Node::select)
							{
								auto laneCount = int32_t(node.sources.size());
								stageIndex -= laneCount * 2;
							}
							if (link.type == Link::toBinary && stageIndex > 0)
							{
								loadTmp = node.tmps[stageIndex - 1];
							}
							doLoad(nodeIndex, workSlotsUsed, linkedNode.sources[link.upstreamOutputIndex], loadTmp);
							workSlotsUsed += 1;
							if (link.type == Link::toSelectZero)
							{
								auto [ laneIndex, sourceIndex ] = toSelectZeroLinkToSourceIndex(link);
								selectStorageSlotSchedule[laneIndex] = doStore(workSlotsUsed - 1, sourceIndex);
							}
						}
					};
					if (node.type == Node::select)
					{
						auto stageCount = int32_t(node.tmps.size() + 1);
						auto laneCount = int32_t(node.sources.size());
						for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
						{
							doLinkUpstream(laneIndex * 2 + 1);
						};
						for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
						{
							doLinkUpstream(laneCount * 2 + stageIndex);
						};
						for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
						{
							doLinkUpstream(laneIndex * 2);
							doCstore(workSlotsUsed - 1, selectStorageSlotSchedule[laneIndex]);
						};
					}
					else
					{
						for (int32_t linkIndicesIndex = 0; linkIndicesIndex < int32_t(node.linkIndices[linkUpstream].size()); ++linkIndicesIndex)
						{
							doLinkUpstream(linkIndicesIndex);
						}
						auto needsStore = false;
						for (auto linkIndex : node.linkIndices[linkDownstream])
						{
							auto &link = tree->links[linkIndex];
							auto linkedNodeIndex = link.directions[linkDownstream].nodeIndex;
							if (nodeIndexToLayerIndex[linkedNodeIndex] != layerIndex)
							{
								needsStore = true;
							}
							if (nodeIndexToLayerIndex[linkedNodeIndex] == layerIndex && link.type == Link::toSelectZero)
							{
								auto [ laneIndex, sourceIndex ] = toSelectZeroLinkToSourceIndex(link);
								selectStorageSlotSchedule[laneIndex] = doStore(workSlotsUsed - 1, sourceIndex);
							}
						}
						if (needsStore)
						{
							doStore(workSlotsUsed - 1, node.sources[0]);
						}
					}
				}
				for (auto &storeScheduleEntry : storeSchedule)
				{
					auto storageSlotIndex = allocStorage(storeScheduleEntry.sourceIndex, false);
					energy.partCount += tree->storeCost;
					if constexpr (std::is_same_v<EnergyType, Plan>)
					{
						energy.steps.push_back(Store{ storeScheduleEntry.workSlotIndex, storageSlotIndex });
					}
					if (storeScheduleEntry.cworkSlotIndex)
					{
						energy.partCount += tree->cstoreCost;
						if constexpr (std::is_same_v<EnergyType, Plan>)
						{
							energy.steps.push_back(Cstore{ *storeScheduleEntry.cworkSlotIndex, storageSlotIndex });
						}
					}
				}
				energy.partCount += tree->commitCost;
				if constexpr (std::is_same_v<EnergyType, Plan>)
				{
					energy.steps.push_back(Commit{});
				}
			}
			auto storageSlotCount = int32_t(slots.size());
			auto storageSlotOverhead = std::max(0, storageSlotCount - tree->storageSlots);
			energy.linear = double(energy.partCount) + double(storageSlotOverhead) * tree->storageSlotOverheadPenalty;
			energy.storageSlotCount = storageSlotCount;
			return energy;
		}
	};

	State Tree::Initial() const
	{
		State state;
		state.tree = this;
		state.iteration = 0;
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
		if (newEnergy < energy)
		{
			return 1.0;
		}
		return std::exp(-(newEnergy - energy) / temp);
	}

	std::istream &operator >>(std::istream &stream, Tree &tree)
	{
		constexpr int32_t bigNumber = 10000;
		std::ios::sync_with_stdio(false);
		int32_t tmpCount;
		stream >> tmpCount >> tree.workSlots >> tree.storageSlots >> CheckCin();
		checkRange(tmpCount, 1, bigNumber);
		checkRange(tree.workSlots, 2, bigNumber);
		checkRange(tree.storageSlots, 1, bigNumber);
		tree.tmps.resize(tmpCount);
		for (auto &tmp : tree.tmps)
		{
			stream >> tmp.commutative >> CheckCin();
		}
		tree.tmps.insert(tree.tmps.begin(), { false });
		stream >> tree.storageSlotOverheadPenalty >> tree.commitCost >> tree.loadCost >> tree.cloadCost >> tree.modeCost >> tree.storeCost >> tree.cstoreCost;
		stream >> tree.constantCount >> tree.inputCount >> tree.compositeCount >> tree.outputCount >> CheckCin();
		checkRange(tree.constantCount, 0, bigNumber);
		checkRange(tree.inputCount, 1, bigNumber);
		checkRange(tree.compositeCount, 1, bigNumber);
		checkRange(tree.outputCount, 1, bigNumber);
		checkRange(tree.inputCount + tree.constantCount, 0, tree.storageSlots + 1);
		checkRange(tree.outputCount + tree.constantCount, 0, tree.storageSlots + 1);
		tree.nodes.resize(tree.constantCount + tree.inputCount + tree.compositeCount + tree.outputCount);
		auto tmpSelect = tmpCount;
		auto maxInnerTmp = tmpCount;
		auto maxOuterTmp = tmpCount + 1;
		auto link = [&tree](Node &node, uint32_t sourceIndex, Link::LinkType linkType) {
			auto &source = tree.sources[sourceIndex];
			source.uses += 1;
			auto nodeIndex = source.nodeIndex;
			auto &linkedNode = tree.nodes[nodeIndex];
			auto linkIndex = tree.links.size();
			Link &link = tree.links.emplace_back();
			link.type = linkType;
			link.directions[linkUpstream].nodeIndex = nodeIndex;
			link.directions[linkDownstream].nodeIndex = &node - &tree.nodes[0];
			link.directions[linkUpstream].linkIndicesIndex = linkedNode.linkIndices[linkDownstream].size();
			link.directions[linkDownstream].linkIndicesIndex = node.linkIndices[linkUpstream].size();
			link.upstreamOutputIndex = source.outputIndex;
			linkedNode.linkIndices[linkDownstream].push_back(linkIndex);
			node.linkIndices[linkUpstream].push_back(linkIndex);
		};
		auto presentSource = [&tree](int32_t nodeIndex, int32_t outputIndex) {
			tree.nodes[nodeIndex].sources.push_back(int32_t(tree.sources.size()));
			tree.sources.push_back({ nodeIndex, outputIndex });
		};
		for (int32_t constantIndex = 0; constantIndex < tree.constantCount; ++constantIndex)
		{
			auto nodeIndex = constantIndex;
			auto &constant = tree.nodes[nodeIndex];
			constant.type = Node::constant;
			presentSource(nodeIndex, 0);
		}
		for (int32_t inputIndex = 0; inputIndex < tree.inputCount; ++inputIndex)
		{
			auto nodeIndex = tree.constantCount + inputIndex;
			auto &input = tree.nodes[nodeIndex];
			input.type = Node::input;
			presentSource(nodeIndex, 0);
		}
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
					checkRange(nonzeroSource, 0, tree.sources.size());
					checkRange(zeroSource, 0, tree.sources.size());
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
						node.tmps[stageIndex - 1] = tmp + 1;
					}
					int32_t source;
					stream >> source >> CheckCin();
					checkRange(source, 0, tree.sources.size());
					link(node, source, Link::toBinary);
				}
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					presentSource(nodeIndex, laneIndex);
				}
			}
			else
			{
				node.type = Node::binary;
				node.tmps.resize(1);
				node.tmps[0] = tmp + 1;
				int32_t rhsSource, lhsSource;
				stream >> rhsSource >> lhsSource >> CheckCin();
				checkRange(rhsSource, 0, tree.sources.size());
				checkRange(lhsSource, 0, tree.sources.size());
				link(node, rhsSource, Link::toBinary);
				link(node, lhsSource, Link::toBinary);
				node.workSlotsNeeded = 2;
				presentSource(nodeIndex, 0);
			}
		}
		for (int32_t outputIndex = 0; outputIndex < tree.outputCount; ++outputIndex)
		{
			auto nodeIndex = tree.constantCount + tree.inputCount + tree.compositeCount + outputIndex;
			auto &node = tree.nodes[nodeIndex];
			int32_t outputSource;
			stream >> outputSource >> CheckCin();
			checkRange(outputSource, 0, tree.sources.size());
			node.type = Node::output;
			link(node, outputSource, Link::toOutput);
		}
		return stream;
	}

	std::ostream &operator <<(std::ostream &stream, const State &state)
	{
		stream << std::setfill('0');
		auto plan = state.GetEnergy<State::Plan>();
		stream << " >>> successful transitions: " << state.iteration << std::endl;
		stream << " >>>     storage slot count: " << plan.storageSlotCount;
		auto showStorageSlots = state.tree->storageSlots;
		if (plan.storageSlotCount > state.tree->storageSlots)
		{
			showStorageSlots = plan.storageSlotCount;
			stream << " (above the desired " << state.tree->storageSlots << ")";
		}
		stream << std::endl;
		stream << " >>>         particle count: " << plan.partCount << std::endl;
		stream << " ";
		for (int32_t columnIndex = 0; columnIndex < showStorageSlots; ++columnIndex)
		{
			stream << "___ ";
		}
		stream << "  ";
		for (int32_t columnIndex = 0; columnIndex < state.tree->workSlots; ++columnIndex)
		{
			stream << "_________ ";
		}
		stream << std::endl;
		int32_t planIndex = 0;
		struct StorageSlot
		{
			int32_t sourceIndex;
			int32_t usesLeft;
		};
		std::vector<StorageSlot> storageSlots(showStorageSlots);
		auto handleStoragePlanStep = [&storageSlots](State::PlanStep &step) {
			if (auto *allocStorage = std::get_if<State::AllocStorage>(&step))
			{
				storageSlots[allocStorage->storageSlot] = { allocStorage->sourceIndex, allocStorage->uses };
			}
			else if (auto *useStorage = std::get_if<State::UseStorage>(&step))
			{
				if (storageSlots[useStorage->storageSlot].usesLeft > 0)
				{
					storageSlots[useStorage->storageSlot].usesLeft -= 1;
				}
			}
		};
		auto emitStorageSlotsTop = [&stream](const std::vector<StorageSlot> &storageSlots) {
			stream << "|";
			for (auto &storageSlot : storageSlots)
			{
				if (storageSlot.usesLeft)
				{
					stream << std::setw(3) << storageSlot.sourceIndex;
				}
				else
				{
					stream << "   ";
				}
				stream << "|";
			}
		};
		auto emitStorageSlotsBottom = [&stream](const std::vector<StorageSlot> &storageSlots) {
			stream << "|";
			for (auto &storageSlot : storageSlots)
			{
				if (storageSlot.usesLeft == -1)
				{
					stream << "__C";
				}
				else if (storageSlot.usesLeft)
				{
					stream << std::setfill('_') << std::setw(3) << storageSlot.usesLeft;
				}
				else
				{
					stream << "___";
				}
				stream << "|";
			}
		};
		while (true)
		{
			auto &step = plan.steps[planIndex];
			planIndex += 1;
			if (std::get_if<State::Commit>(&step))
			{
				break;
			}
			handleStoragePlanStep(step);
		}
		for (int32_t layerIndex = 1; layerIndex < int32_t(state.layers.size()) - 1; ++layerIndex)
		{
			auto storageSlotsCopy = storageSlots;
			struct WorkSlotState
			{
				bool triggeredMode = false;
				std::optional<int32_t> tmp;
				std::optional<int32_t> loadedFrom;
				std::optional<int32_t> cloadedFrom;
				std::optional<int32_t> storedTo;
				std::optional<int32_t> cstoredTo;
				std::optional<int32_t> nodeIndex;
			};
			std::vector<WorkSlotState> workSlotStates(state.tree->workSlots);
			while (true)
			{
				auto &step = plan.steps[planIndex];
				planIndex += 1;
				if (std::get_if<State::Commit>(&step))
				{
					break;
				}
				else if (auto *load = std::get_if<State::Load>(&step))
				{
					workSlotStates[load->workSlot].tmp = load->tmp;
					workSlotStates[load->workSlot].loadedFrom = load->storageSlot;
					workSlotStates[load->workSlot].nodeIndex = load->nodeIndex;
				}
				else if (auto *cload = std::get_if<State::Cload>(&step))
				{
					workSlotStates[cload->workSlot].tmp = cload->tmp;
					workSlotStates[cload->workSlot].cloadedFrom = cload->storageSlot;
					workSlotStates[cload->workSlot].nodeIndex = cload->nodeIndex;
				}
				else if (auto *store = std::get_if<State::Store>(&step))
				{
					workSlotStates[store->workSlot].storedTo = store->storageSlot;
				}
				else if (auto *cstore = std::get_if<State::Cstore>(&step))
				{
					workSlotStates[cstore->workSlot].cstoredTo = cstore->storageSlot;
				}
				else if (auto *mode = std::get_if<State::Mode>(&step))
				{
					workSlotStates[mode->workSlot].triggeredMode = true;
				}
				handleStoragePlanStep(step);
			}
			emitStorageSlotsTop(storageSlotsCopy);
			stream << " |";
			for (auto &workSlotState : workSlotStates)
			{
				if (workSlotState.nodeIndex)
				{
					if (workSlotState.cloadedFrom)
					{
						stream << std::setw(2) << *workSlotState.cloadedFrom;
						stream << "/" << std::setw(1) << *workSlotState.tmp << ">>";
					}
					if (workSlotState.loadedFrom)
					{
						stream << std::setw(2) << *workSlotState.loadedFrom;
						stream << "/" << std::setw(1) << *workSlotState.tmp << "->";
					}
					stream << std::setw(3) << state.tree->nodes[*workSlotState.nodeIndex].sources[0];
				}
				else
				{
					stream << "         ";
				}
				stream << "|";
			}
			stream << std::endl;
			emitStorageSlotsBottom(storageSlotsCopy);
			stream << " |" << std::setfill('0');
			for (auto &workSlotState : workSlotStates)
			{
				if (workSlotState.nodeIndex)
				{
					if (workSlotState.triggeredMode)
					{
						stream << "*";
					}
					else
					{
						stream << "_";
					}
					if (workSlotState.cstoredTo)
					{
						stream << ">>" << std::setw(2) << *workSlotState.cstoredTo;
					}
					else
					{
						stream << "____";
					}
					if (workSlotState.storedTo)
					{
						stream << "->" << std::setw(2) << *workSlotState.storedTo;
					}
					else
					{
						stream << "____";
					}
				}
				else
				{
					stream << "_________";
				}
				stream << "|";
			}
			stream << std::endl;
			// auto layerBegin = state.LayerBegins(layerIndex);
			// auto layerEnd = state.LayerBegins(layerIndex + 1);
			// for (auto nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
			// {
			// 	auto nodeIndex = state.nodeIndices[nodeIndicesIndex];
			// 	stream << std::setfill('0') << std::setw(3) << nodeIndex << " ";
			// }
		}
		emitStorageSlotsTop(storageSlots);
		stream << std::endl;
		emitStorageSlotsBottom(storageSlots);
		stream << std::endl;
		stream << std::endl;
		stream << std::endl;
		return stream;
	}
}

int main()
{
	constexpr auto tempInit = 1.0;
	constexpr auto tempFini = 1e-5;
	constexpr auto tempLoss = 1e-7;
	std::random_device rd;
	std::mt19937_64 rng(rd());
	std::uniform_real_distribution<double> rdist(0.0, 1.0);
	Tree tree;
	std::cin >> tree;
	State state = tree.Initial();
	std::cerr << state;
	auto temp = tempInit;
	int32_t count = 0;
	while (temp > tempFini)
	{
		auto newState = state.RandomNeighbour(rng);
		auto energyLinear = state.GetEnergy<State::Energy>().linear;
		auto newEnergyLinear = newState.GetEnergy<State::Energy>().linear;
		if (TransitionProbability(energyLinear, newEnergyLinear, temp) >= rdist(rng))
		{
			state = std::move(newState);
		}
		count += 1;
		if (count == 100000)
		{
			count = 0;
			std::cerr << state;
		}
		temp -= tempLoss;
		// temp *= tempLoss;
	}
	return 0;
}
