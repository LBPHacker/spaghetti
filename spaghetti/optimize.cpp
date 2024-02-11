#include "optimize.hpp"
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <iomanip>
#include <iterator>
#include <mutex>

namespace
{
	static void CheckRange(int32_t v, int32_t l, int32_t h, int errAt)
	{
		if (v < l || v >= h)
		{
			throw RangeCheckFailed("failed CheckRange at line " + std::to_string(errAt));
			exit(1);
		}
	}
#define CheckRange(v, l, h) CheckRange((v), (l), (h), __LINE__)

	struct Layer
	{
		std::vector<int32_t> nodeIndices;
		int32_t workSlotsUsed;
	};

	std::array<int32_t, tmpCount> tmpCommutativity = {{ 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 }};

	constexpr int32_t lsnsLife3Value  = 0x10000003;
}

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
		for (auto dir = LinkDirection(0); dir < linkMax; dir = LinkDirection(int32_t(dir) + 1))
		{
			for (auto linkIndex : node.linkIndices[dir])
			{
				auto &link = design->links[linkIndex];
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

std::vector<Move> State::PossibleMoves() const
{
	auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
	std::vector<Move> moves;
	for (int32_t compositeIndex = 0; compositeIndex < design->compositeCount; ++compositeIndex)
	{
		auto nodeIndex = design->constantCount + design->inputCount + compositeIndex;
		auto &node = design->nodes[nodeIndex];
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
				auto &link = design->links[linkIndex];
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

std::optional<Move> State::RandomValidMove(std::mt19937_64 &rng) const
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

void EnergyWithPlan::MoveWorkSlot0GroupsBack()
{
	// subtle: work slot 0 is guaranteed to be tmp = 0 so we move the group of load+cloads
	//         that loads it to the end, so at the end of the mode/load/cload phase,
	//         the filt directly on the left of the stack has the same tmp and ctype as
	//         the filt of work slot 0; TODO: make use of this by merging them
	bool inTmpGroup;
	bool groupLoadsWorkSlot0;
	int32_t groupBeginsAt;
	int32_t workSlot0GroupBeginsAt;
	int32_t workSlot0GroupEndsAt;
	auto reset = [&]() {
		inTmpGroup = false;
		groupLoadsWorkSlot0 = false;
		groupBeginsAt = -1;
		workSlot0GroupBeginsAt = -1;
		workSlot0GroupEndsAt = -1;
	};
	reset();
	for (int32_t stepIndex = 0; stepIndex < int32_t(steps.size()); ++stepIndex)
	{
		auto &step = steps[stepIndex];
		if (std::get_if<EnergyWithPlan::Mode>(&step))
		{
			inTmpGroup = true;
		}
		else if (auto *load = std::get_if<EnergyWithPlan::Load>(&step))
		{
			if (groupLoadsWorkSlot0)
			{
				workSlot0GroupBeginsAt = groupBeginsAt;
				workSlot0GroupEndsAt = stepIndex;
			}
			groupLoadsWorkSlot0 = false;
			groupBeginsAt = stepIndex;
			if (load->workSlot == 0)
			{
				groupLoadsWorkSlot0 = true;
			}
		}
		else if (auto *cload = std::get_if<EnergyWithPlan::Cload>(&step))
		{
			if (cload->workSlot == 0)
			{
				groupLoadsWorkSlot0 = true;
			}
		}
		else
		{
			if (inTmpGroup)
			{
				auto tmpGroupEndsAt = stepIndex;
				std::rotate(
					steps.begin() + workSlot0GroupBeginsAt,
					steps.begin() + workSlot0GroupEndsAt,
					steps.begin() + tmpGroupEndsAt
				);
			}
			reset();
		}
	}
}

void EnergyWithPlan::SortSteps()
{
	std::sort(steps.begin(), steps.end(), [](auto &lhs, auto &rhs) {
		auto layerIndex = [](auto &step) {
			return std::visit([](auto &thing) {
				return thing.layerIndex;
			}, step);
		};
		auto lhsLayerIndex = layerIndex(lhs);
		auto rhsLayerIndex = layerIndex(rhs);
		if (lhsLayerIndex != rhsLayerIndex)
		{
			return lhsLayerIndex < rhsLayerIndex;
		}
		// at this point only order within the layer needs to be established
		auto layerOrder = [](auto &step) {
			return std::visit([](auto &thing) {
				return thing.layerOrder;
			}, step);
		};
		auto lhsLayerOrder = layerOrder(lhs);
		auto rhsLayerOrder = layerOrder(rhs);
		if (lhsLayerOrder != rhsLayerOrder)
		{
			return lhsLayerOrder < rhsLayerOrder;
		}
		if (lhsLayerOrder == Load::layerOrder)
		{
			// at this point only order among Mode, Load, and Cload needs to be established
			auto order = [](auto &step) -> std::tuple<int32_t, int32_t, int32_t, int32_t> {
				// subtle: the tmp = 0 group ends up being ordered last so the tmp of the filt directly
				//         on the left of the stack is 0, which means it doesn't affect the bray
				if (auto *load = std::get_if<EnergyWithPlan::Load>(&step))
				{
					return { -load->tmp, 1, -load->storageSlot, 0 };
				}
				else if (auto *cload = std::get_if<EnergyWithPlan::Cload>(&step))
				{
					return { -cload->tmp, 1, -cload->storageSlot, 1 };
				}
				else if (auto *mode = std::get_if<EnergyWithPlan::Mode>(&step))
				{
					return { -mode->tmp, 0, -1, -1 };
				}
				return { -1, -1, -1, -1 };
			};
			auto lhsOrder = order(lhs);
			auto rhsOrder = order(rhs);
			if (lhsOrder != rhsOrder)
			{
				return lhsOrder < rhsOrder;
			}
		}
		if (lhsLayerOrder == Store::layerOrder)
		{
			// at this point only order among Store and Cstore needs to be established
			auto order = [](auto &step) -> std::tuple<int32_t, int32_t> {
				if (auto *store = std::get_if<EnergyWithPlan::Store>(&step))
				{
					return { store->storageSlot, 1 };
				}
				else if (auto *cstore = std::get_if<EnergyWithPlan::Cstore>(&step))
				{
					return { cstore->storageSlot, 0 };
				}
				return { -1, -1 };
			};
			auto lhsOrder = order(lhs);
			auto rhsOrder = order(rhs);
			if (lhsOrder != rhsOrder)
			{
				return lhsOrder < rhsOrder;
			}
		}
		return false;
	});
	MoveWorkSlot0GroupsBack();
}

std::shared_ptr<Plan> EnergyWithPlan::ToPlan() const
{
	if (outputRemapFailed)
	{
		throw OutputRemappingFailed();
	}
	if (design->storageSlots < storageSlotCount)
	{
		throw StorageSlotBudgetExceeded();
	}
	if (design->workSlots < workSlotCount)
	{
		throw WorkSlotBudgetExceeded();
	}
	auto bottomTopCost = Plan::Bottom::cost + Plan::Top::cost;
	auto stackLayersMaxCost = design->StackMaxCost() - bottomTopCost;
	auto plan = std::make_shared<Plan>();
	int32_t lsnsLife3Index = -1;
	std::vector<int32_t> constantValue(design->storageSlots, 0xF0000000);
	for (auto clobberStorageSlot : design->clobberStorageSlots)
	{
		constantValue[clobberStorageSlot] = 0xF000DEAD;
	}
	for (int32_t inputIndex = 0; inputIndex < design->inputCount; ++inputIndex)
	{
		constantValue[design->inputStorageSlots[inputIndex]] = 0xF000C0DE;
	}
	for (auto &step : steps)
	{
		if (auto *constant = std::get_if<Constant>(&step))
		{
			constantValue[constant->storageSlot] = constant->value;
			if (constant->value == lsnsLife3Value)
			{
				lsnsLife3Index = constant->storageSlot;
			}
		}
	}
	plan->steps.push_back(Plan::Lcap{ { 0 }, lsnsLife3Index });
	struct Buffer
	{
		std::vector<Plan::Step> steps;
		int32_t cost = 0;
	};
	Buffer stackBuffer;
	Buffer layerBuffer;
	auto pushToBuffer = [](Buffer &buffer, Plan::Step step) {
		buffer.steps.push_back(step);
		buffer.cost += std::visit([](auto &step) {
			return step.cost;
		}, step);
	};
	int32_t stackIndex = 0;
	auto flushStack = [this, &stackBuffer, &plan, &stackIndex]() {
		if (stackBuffer.cost)
		{
			if (stackIndex >= design->stacks)
			{
				throw StackBudgetExceeded();
			}
			plan->steps.push_back(Plan::Bottom{ stackIndex });
			for (auto item : stackBuffer.steps)
			{
				std::visit([stackIndex](auto &item) {
					item.stackIndex = stackIndex;
				}, item);
				plan->steps.push_back(item);
			}
			plan->steps.push_back(Plan::Top{ stackIndex });
			stackIndex += 1;
			stackBuffer = {};
		}
	};
	auto pushToLayer = [&layerBuffer, &pushToBuffer](Plan::Step step) {
		pushToBuffer(layerBuffer, step);
	};
	auto layerOpen = false;
	auto beganStore = false;
	auto flushLayer = [
		&stackIndex,
		&beganStore,
		&layerOpen,
		&pushToLayer,
		&stackBuffer,
		&layerBuffer,
		&flushStack,
		stackLayersMaxCost
	]() {
		if (layerOpen)
		{
			layerOpen = false;
			beganStore = false;
			pushToLayer(Plan::West{ stackIndex });
			pushToLayer(Plan::Clear{ stackIndex });
			assert(layerBuffer.cost <= stackLayersMaxCost);
			if (stackBuffer.cost + layerBuffer.cost > stackLayersMaxCost)
			{
				flushStack();
			}
			stackBuffer.steps.insert(stackBuffer.steps.end(), layerBuffer.steps.begin(), layerBuffer.steps.end());
			stackBuffer.cost += layerBuffer.cost;
			layerBuffer = {};
		}
	};
	auto beginLayer = [&layerOpen]() {
		if (!layerOpen)
		{
			layerOpen = true;
		}
	};
	auto beginStore = [&beganStore, &stackIndex, &beginLayer, &pushToLayer]() {
		beginLayer();
		if (!beganStore)
		{
			beganStore = true;
			pushToLayer(Plan::Aray{ stackIndex });
			pushToLayer(Plan::East{ stackIndex });
		}
	};
	std::vector<int32_t> slotIsVoid(design->storageSlots, 0); // std::vector<bool> is stupid
	for (int32_t voidIndex = 0; voidIndex < int32_t(design->voidStorageSlots.size()); ++voidIndex)
	{
		slotIsVoid[design->voidStorageSlots[voidIndex]] = 1;
	}
	for (int32_t storageSlotIndex = 0; storageSlotIndex < design->storageSlots; ++storageSlotIndex)
	{
		if (!slotIsVoid[storageSlotIndex])
		{
			plan->steps.push_back(Plan::Rfilt{ { 0 }, storageSlotIndex, constantValue[storageSlotIndex] });
		}
	}
	for (int32_t workSlotIndex = 0; workSlotIndex < design->workSlots; ++workSlotIndex)
	{
		plan->steps.push_back(Plan::Lfilt{ { 0 }, workSlotIndex });
	}
	for (auto &step : steps)
	{
		if (std::get_if<Commit>(&step))
		{
			flushLayer();
		}
		else if (auto *load = std::get_if<Load>(&step))
		{
			beginLayer();
			pushToLayer(Plan::Load{ { -1 }, load->workSlot, load->storageSlot });
		}
		else if (auto *cload = std::get_if<Cload>(&step))
		{
			beginLayer();
			pushToLayer(Plan::Cload{ { -1 }, cload->workSlot });
		}
		else if (auto *store = std::get_if<Store>(&step))
		{
			beginStore();
			pushToLayer(Plan::Store{ { -1 }, store->workSlot, store->storageSlot });
		}
		else if (auto *cstore = std::get_if<Cstore>(&step))
		{
			beginStore();
			pushToLayer(Plan::Cstore{ { -1 }, cstore->workSlot, cstore->storageSlot });
		}
		else if (auto *mode = std::get_if<Mode>(&step))
		{
			beginLayer();
			pushToLayer(Plan::Mode{ { -1 }, mode->tmp });
		}
	}
	flushStack();
	for (auto &step : plan->steps)
	{
		std::visit([&plan](auto &step) {
			plan->cost += step.cost;
		}, step);
	}
	plan->stacksUsed = stackIndex;
	while (stackIndex < design->stacks)
	{
		plan->steps.push_back(Plan::Bottom{ stackIndex });
		plan->steps.push_back(Plan::Top{ stackIndex });
		stackIndex += 1;
	}
	return plan;
}

template<class EnergyType>
EnergyType State::GetEnergy() const
{
	EnergyType energy;
	struct OutputRemap
	{
		int32_t from, to;
	};
	std::vector<OutputRemap> outputRemaps;
	auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
	struct Storage
	{
		int32_t usesLeft = 0;
		int32_t slotIndex = -1;
		std::vector<int32_t> outputLinks;
	};
	std::vector<std::optional<int32_t>> slots;
	std::vector<Storage> storage(design->sources.size());
	std::vector<int32_t> disallowConstantsInSlots(design->storageSlots, 0); // std::vector<bool> is stupid
	for (auto &outputLink : design->outputLinks)
	{
		storage[outputLink.sourceIndex].outputLinks.push_back(outputLink.storageSlot);
		disallowConstantsInSlots[outputLink.storageSlot] = 1;
	}
	for (auto clobberStorageSlot : design->clobberStorageSlots)
	{
		disallowConstantsInSlots[clobberStorageSlot] = 1;
	}
	enum StorageUsage
	{
		usageNormal,
		usageConstant,
		usageVoid,
	};
	auto allocStorage = [
		this,
		&outputRemaps,
		&energy,
		&storage,
		&disallowConstantsInSlots,
		&slots
	](int32_t layerIndex, int32_t sourceIndex, StorageUsage usage, std::optional<int32_t> freeSlotIndex) {
		if (usage != usageVoid)
		{
			for (auto slotIndex : storage[sourceIndex].outputLinks)
			{
				if (int32_t(slots.size()) < slotIndex + 1)
				{
					slots.resize(slotIndex + 1);
				}
				if (!freeSlotIndex && !slots[slotIndex])
				{
					freeSlotIndex = slotIndex;
				}
			}
		}
		if (freeSlotIndex)
		{
			auto minSize = *freeSlotIndex + 1;
			if (int32_t(slots.size()) < minSize)
			{
				slots.resize(minSize);
			}
		}
		auto slotOk = [&slots, &disallowConstantsInSlots, usage](int32_t slotIndex) {
			return !slots[slotIndex] && !(usage == usageConstant && slotIndex < int32_t(disallowConstantsInSlots.size()) && disallowConstantsInSlots[slotIndex]);
		};
		if (!freeSlotIndex)
		{
			for (int32_t slotIndex = 0; slotIndex < int32_t(slots.size()); ++slotIndex)
			{
				if (slotOk(slotIndex))
				{
					freeSlotIndex = slotIndex;
					break;
				}
			}
		}
		while (!freeSlotIndex)
		{
			auto tryNext = slots.size();
			slots.emplace_back();
			if (slotOk(tryNext))
			{
				freeSlotIndex = tryNext;
			}
		}
		assert(!slots[*freeSlotIndex]);
		slots[*freeSlotIndex] = sourceIndex; // outputRemaps
		if (usage != usageVoid)
		{
			for (auto slotIndex : storage[sourceIndex].outputLinks)
			{
				if (slotIndex != *freeSlotIndex)
				{
					outputRemaps.push_back({ *freeSlotIndex, slotIndex });
				}
			}
			auto uses = design->sources[sourceIndex].uses;
			if (usage == usageConstant)
			{
				uses = -1; // constants have infinite uses
			}
			storage[sourceIndex].usesLeft = uses;
			storage[sourceIndex].slotIndex = *freeSlotIndex;
			if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
			{
				energy.steps.push_back(EnergyWithPlan::AllocStorage{ { layerIndex }, sourceIndex, *freeSlotIndex, uses });
			}
		}
		return *freeSlotIndex;
	};
	auto useStorage = [&energy, &storage, &slots](int32_t layerIndex, int32_t sourceIndex) {
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
		if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
		{
			energy.steps.push_back(EnergyWithPlan::UseStorage{ { layerIndex }, slotIndex });
		}
		return slotIndex;
	};
	for (int32_t inputIndex = 0; inputIndex < design->inputCount; ++inputIndex)
	{
		auto nodeIndex = design->constantCount + inputIndex;
		auto &node = design->nodes[nodeIndex];
		auto sourceIndex = node.sources[0];
		allocStorage(0, sourceIndex, usageNormal, design->inputStorageSlots[inputIndex]);
	}
	for (int32_t voidIndex = 0; voidIndex < int32_t(design->voidStorageSlots.size()); ++voidIndex)
	{
		allocStorage(0, -1, usageVoid, design->voidStorageSlots[voidIndex]);
	}
	for (int32_t constantIndex = 0; constantIndex < design->constantCount; ++constantIndex)
	{
		auto nodeIndex = constantIndex;
		auto &node = design->nodes[nodeIndex];
		auto sourceIndex = node.sources[0];
		auto storageSlotIndex = allocStorage(0, sourceIndex, usageConstant, std::nullopt);
		if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
		{
			energy.steps.push_back(EnergyWithPlan::Constant{ { 0 }, storageSlotIndex, design->constantValues[constantIndex] });
		}
	}
	if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
	{
		energy.steps.push_back(EnergyWithPlan::Commit{ 0 });
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
		auto toSelectZeroLinkToSourceIndex = [this](const Link &link) {
			auto &node = design->nodes[link.directions[linkDownstream].nodeIndex];
			auto laneIndex = (link.directions[linkDownstream].linkIndicesIndex - 1) / 2;
			return std::pair<int32_t, int32_t>{ laneIndex, node.sources[laneIndex] };
		};
		auto doStore = [&storeSchedule](int32_t workSlotIndex, int32_t sourceIndex) {
			auto storeScheduleIndex = int32_t(storeSchedule.size());
			storeSchedule.push_back({ sourceIndex, workSlotIndex, {} });
			return storeScheduleIndex;
		};
		std::vector<int32_t> selectStorageSlotSchedule;
		auto doCstore = [&storeSchedule, &toSelectZeroLinkToSourceIndex, &selectStorageSlotSchedule](int32_t workSlotIndex, const Link &link) {
			auto storeScheduleIndex = int32_t(storeSchedule.size());
			auto [ laneIndex, sourceIndex ] = toSelectZeroLinkToSourceIndex(link);
			storeSchedule.push_back({ sourceIndex, -1, workSlotIndex });
			selectStorageSlotSchedule[laneIndex] = storeScheduleIndex;
		};
		auto doCstoreStore = [&storeSchedule](int32_t workSlotIndex, int32_t storeScheduleIndex) {
			storeSchedule[storeScheduleIndex].workSlotIndex = workSlotIndex;
		};
		struct TmpLoad
		{
			bool used;
			std::vector<int32_t> slotUsed; // std::vector<bool> is stupid
		};
		std::vector<TmpLoad> tmpLoads(tmpCount, { false, std::vector<int32_t>(slots.size(), 0) });
		auto doLoad = [&energy, &useStorage, &tmpLoads, layerIndex](int32_t nodeIndex, int32_t workSlotIndex, int32_t sourceIndex, int32_t tmp) {
			auto storageSlotIndex = useStorage(layerIndex, sourceIndex);
			if (!tmpLoads[tmp].used)
			{
				energy.partCount += Plan::Mode::cost;
				if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
				{
					energy.steps.push_back(EnergyWithPlan::Mode{ { layerIndex }, workSlotIndex, tmp });
				}
				tmpLoads[tmp].used = true;
			}
			if (tmpLoads[tmp].slotUsed[storageSlotIndex])
			{
				energy.partCount += Plan::Cload::cost;
				if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
				{
					energy.steps.push_back(EnergyWithPlan::Cload{ { layerIndex }, nodeIndex, tmp, workSlotIndex, storageSlotIndex });
				}
			}
			else
			{
				tmpLoads[tmp].slotUsed[storageSlotIndex] = 1;
				energy.partCount += Plan::Load::cost;
				if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
				{
					energy.steps.push_back(EnergyWithPlan::Load{ { layerIndex }, nodeIndex, tmp, workSlotIndex, storageSlotIndex });
				}
			}
		};
		auto layerBegin = LayerBegins(layerIndex);
		auto layerEnd = LayerBegins(layerIndex + 1);
		int32_t workSlotsUsed = 0;
		auto &lastNode = design->nodes[nodeIndices[layerEnd - 1]];
		if (lastNode.type == Node::select)
		{
			selectStorageSlotSchedule.resize(lastNode.sources.size(), -1);
		}
		auto doLinkUpstream = [
			this,
			&nodeIndexToLayerIndex,
			&workSlotsUsed,
			&doLoad,
			&doCstore,
			layerIndex
		](int32_t nodeIndex, int32_t linkIndicesIndex) {
			auto &node = design->nodes[nodeIndex];
			auto linkIndex = node.linkIndices[linkUpstream][linkIndicesIndex];
			auto &link = design->links[linkIndex];
			auto linkedNodeIndex = link.directions[linkUpstream].nodeIndex;
			auto &linkedNode = design->nodes[linkedNodeIndex];
			if (nodeIndexToLayerIndex[linkedNodeIndex] != layerIndex)
			{
				auto loadTmp = 0;
				auto stageIndex = linkIndicesIndex;
				if (node.type == Node::select)
				{
					auto laneCount = int32_t(node.sources.size());
					stageIndex -= laneCount * 2;
				}
				if (link.type == Link::toBinary && stageIndex == 0)
				{
					// grab stage 1 tmp if it's coming from the same layer
					auto linkIndexNext = node.linkIndices[linkUpstream][linkIndicesIndex + 1];
					auto &linkNext = design->links[linkIndexNext];
					auto linkedNodeNextIndex = linkNext.directions[linkUpstream].nodeIndex;
					if (nodeIndexToLayerIndex[linkedNodeNextIndex] == layerIndex)
					{
						stageIndex += 1;
					}
				}
				if (link.type == Link::toBinary && stageIndex > 0)
				{
					loadTmp = node.tmps[stageIndex - 1];
				}
				doLoad(nodeIndex, workSlotsUsed, linkedNode.sources[link.upstreamOutputIndex], loadTmp);
				workSlotsUsed += 1;
				if (link.type == Link::toSelectZero)
				{
					doCstore(workSlotsUsed - 1, link);
				}
			}
		};
		for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
		{
			auto nodeIndex = nodeIndices[nodeIndicesIndex];
			auto &node = design->nodes[nodeIndex];
			if (node.type == Node::select)
			{
				// do zeros first so they don't get inserted between the cond input and its same-layer source
				auto laneCount = int32_t(node.sources.size());
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					doLinkUpstream(nodeIndex, laneIndex * 2 + 1);
				};
			}
		}
		for (int32_t nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
		{
			auto nodeIndex = nodeIndices[nodeIndicesIndex];
			auto &node = design->nodes[nodeIndex];
			if (node.type == Node::select)
			{
				auto stageCount = int32_t(node.tmps.size() + 1);
				auto laneCount = int32_t(node.sources.size());
				for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
				{
					doLinkUpstream(nodeIndex, laneCount * 2 + stageIndex);
				};
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					doLinkUpstream(nodeIndex, laneIndex * 2);
					doCstoreStore(workSlotsUsed - 1, selectStorageSlotSchedule[laneIndex]);
				};
			}
			else
			{
				for (int32_t linkIndicesIndex = 0; linkIndicesIndex < int32_t(node.linkIndices[linkUpstream].size()); ++linkIndicesIndex)
				{
					doLinkUpstream(nodeIndex, linkIndicesIndex);
				}
				auto needsStore = false;
				for (auto linkIndex : node.linkIndices[linkDownstream])
				{
					auto &link = design->links[linkIndex];
					auto linkedNodeIndex = link.directions[linkDownstream].nodeIndex;
					if (nodeIndexToLayerIndex[linkedNodeIndex] != layerIndex)
					{
						needsStore = true;
					}
					if (nodeIndexToLayerIndex[linkedNodeIndex] == layerIndex && link.type == Link::toSelectZero)
					{
						doCstore(workSlotsUsed - 1, link);
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
			auto storageSlotIndex = allocStorage(layerIndex, storeScheduleEntry.sourceIndex, usageNormal, std::nullopt);
			energy.partCount += Plan::Store::cost;
			if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
			{
				energy.steps.push_back(EnergyWithPlan::Store{ { layerIndex }, storeScheduleEntry.workSlotIndex, storageSlotIndex });
			}
			if (storeScheduleEntry.cworkSlotIndex)
			{
				energy.partCount += Plan::Cstore::cost;
				if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
				{
					energy.steps.push_back(EnergyWithPlan::Cstore{ { layerIndex }, *storeScheduleEntry.cworkSlotIndex, storageSlotIndex });
				}
			}
		}
		energy.partCount += Plan::commitCost;
		if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
		{
			energy.steps.push_back(EnergyWithPlan::Commit{ layerIndex });
		}
	}
	auto storageSlotCount = int32_t(slots.size());
	auto storageSlotOverhead = std::max(0, storageSlotCount - design->storageSlots);
	auto workSlotCount = 0;
	for (int32_t layerIndex = 1; layerIndex < int32_t(layers.size()) - 1; ++layerIndex)
	{
		auto layerBegin = LayerBegins(layerIndex);
		auto layerEnd = LayerBegins(layerIndex + 1);
		auto result = design->CheckLayer(nodeIndices.begin() + layerBegin, nodeIndices.begin() + layerEnd);
		workSlotCount = std::max(workSlotCount, result->workSlots);
	}
	auto workSlotOverhead = std::max(0, workSlotCount - design->workSlots);
	energy.linear = double(energy.partCount) +
	                double(storageSlotOverhead) * design->storageSlotOverheadPenalty +
	                double(workSlotOverhead) * design->workSlotOverheadPenalty;
	energy.storageSlotCount = storageSlotCount;
	energy.workSlotCount = workSlotCount;
	energy.design = design;
	if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
	{
		if (int32_t(outputRemaps.size()) > design->workSlots)
		{
			energy.outputRemapFailed = true;
		}
		else if (outputRemaps.size())
		{
			auto layerIndex = int32_t(layers.size()) - 1;
			energy.steps.push_back(EnergyWithPlan::Mode{ { layerIndex }, 0, 0 });
			for (int32_t outputRemapIndex = 0; outputRemapIndex < int32_t(outputRemaps.size()); ++outputRemapIndex)
			{
				auto &outputRemap = outputRemaps[outputRemapIndex];
				energy.steps.push_back(EnergyWithPlan::Load{ { layerIndex }, -1, 0, outputRemapIndex, outputRemap.from });
				energy.steps.push_back(EnergyWithPlan::Store{ { layerIndex }, outputRemapIndex, outputRemap.to });
			}
			energy.steps.push_back(EnergyWithPlan::Commit{ layerIndex });
		}
		energy.SortSteps();
	}
	return energy;
}

template Energy State::GetEnergy<Energy>() const;
template EnergyWithPlan State::GetEnergy<EnergyWithPlan>() const;

std::shared_ptr<State> Design::Initial() const
{
	auto state = std::make_shared<State>();
	state->design = shared_from_this();
	state->iteration = 0;
	for (int32_t nodeIndex = 0; nodeIndex < int32_t(nodes.size()); ++nodeIndex)
	{
		state->nodeIndices.push_back(nodeIndex);
	}
	state->layers.push_back(0);
	for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
	{
		state->layers.push_back(constantCount + inputCount + compositeIndex);
	}
	state->layers.push_back(constantCount + inputCount + compositeCount);
	return state;
}

std::ostream &operator <<(std::ostream &stream, const State &state)
{
	stream << std::setfill('0');
	auto plan = state.GetEnergy<EnergyWithPlan>();
	stream << " >>> successful transitions: " << state.iteration << std::endl;
	stream << " >>>     storage slot count: " << plan.storageSlotCount;
	auto showStorageSlots = state.design->storageSlots;
	if (plan.storageSlotCount > state.design->storageSlots)
	{
		showStorageSlots = plan.storageSlotCount;
		stream << " (above the desired " << state.design->storageSlots << ")";
	}
	stream << std::endl;
	stream << " >>>         particle count: " << plan.partCount << std::endl;
	stream << " ";
	for (int32_t columnIndex = 0; columnIndex < showStorageSlots; ++columnIndex)
	{
		stream << "___ ";
	}
	stream << "  ";
	for (int32_t columnIndex = 0; columnIndex < state.design->workSlots; ++columnIndex)
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
	auto handleStoragePlanStep = [&storageSlots](auto &step) {
		if (auto *allocStorage = std::get_if<EnergyWithPlan::AllocStorage>(&step))
		{
			storageSlots[allocStorage->storageSlot] = { allocStorage->sourceIndex, allocStorage->uses };
		}
		else if (auto *useStorage = std::get_if<EnergyWithPlan::UseStorage>(&step))
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
		if (std::get_if<EnergyWithPlan::Commit>(&step))
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
		std::vector<WorkSlotState> workSlotStates(state.design->workSlots);
		while (true)
		{
			auto &step = plan.steps[planIndex];
			planIndex += 1;
			if (std::get_if<EnergyWithPlan::Commit>(&step))
			{
				break;
			}
			else if (auto *load = std::get_if<EnergyWithPlan::Load>(&step))
			{
				workSlotStates[load->workSlot].tmp = load->tmp;
				workSlotStates[load->workSlot].loadedFrom = load->storageSlot;
				workSlotStates[load->workSlot].nodeIndex = load->nodeIndex;
			}
			else if (auto *cload = std::get_if<EnergyWithPlan::Cload>(&step))
			{
				workSlotStates[cload->workSlot].tmp = cload->tmp;
				workSlotStates[cload->workSlot].cloadedFrom = cload->storageSlot;
				workSlotStates[cload->workSlot].nodeIndex = cload->nodeIndex;
			}
			else if (auto *store = std::get_if<EnergyWithPlan::Store>(&step))
			{
				workSlotStates[store->workSlot].storedTo = store->storageSlot;
			}
			else if (auto *cstore = std::get_if<EnergyWithPlan::Cstore>(&step))
			{
				workSlotStates[cstore->workSlot].cstoredTo = cstore->storageSlot;
			}
			else if (auto *mode = std::get_if<EnergyWithPlan::Mode>(&step))
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
					stream << "/" << std::setw(1) << std::hex << std::uppercase << *workSlotState.tmp << std::dec << ">>";
				}
				if (workSlotState.loadedFrom)
				{
					stream << std::setw(2) << *workSlotState.loadedFrom;
					stream << "/" << std::setw(1) << std::hex << std::uppercase << *workSlotState.tmp << std::dec << "->";
				}
				stream << std::setw(3) << state.design->nodes[*workSlotState.nodeIndex].sources[0];
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
	}
	emitStorageSlotsTop(storageSlots);
	stream << std::endl;
	emitStorageSlotsBottom(storageSlots);
	stream << std::endl;
	stream << std::endl;
	stream << std::endl;
	return stream;
}

std::optional<Design::CheckResult> Design::CheckLayer(NodeIndexIterator nodeIndicesBegin, NodeIndexIterator nodeIndicesEnd) const
{
	auto size = nodeIndicesEnd - nodeIndicesBegin;
	// we assume that node order between layers is correct
	// but we detect node order violations within the layer
	for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(size) - 1; ++nodeIndicesIndex)
	{
		auto &node = nodes[nodeIndicesBegin[nodeIndicesIndex]];
		if (node.type == Node::select)
		{
			// select somewhere other than at the end
			return std::nullopt;
		}
	}
	CheckResult checkResult;
	checkResult.workSlots = 0;
	auto nodeIndexInLayer = [nodeIndicesBegin, nodeIndicesEnd](int32_t nodeIndex) -> std::optional<int32_t> {
		auto it = std::find(nodeIndicesBegin, nodeIndicesEnd, nodeIndex);
		if (it == nodeIndicesEnd)
		{
			return std::nullopt;
		}
		return int32_t(it - nodeIndicesBegin);
	};
	for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(size); ++nodeIndicesIndex)
	{
		auto &node = nodes[nodeIndicesBegin[nodeIndicesIndex]];
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
					if (link.directions[linkDownstream].linkIndicesIndex == lhsIndex && !tmpCommutativity[linkedNode.tmps[0]])
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
	return checkResult;
}

Design::Design(
	int32_t newStacks,
	int32_t newWorkSlots,
	int32_t newStackMaxSize,
	int32_t newStorageSlots,
	double newStorageSlotOverheadPenalty,
	double newWorkSlotOverheadPenalty,
	std::vector<int32_t> newConstantValues,
	std::vector<int32_t> newInputStorageSlots,
	std::vector<int32_t> newClobberStorageSlots,
	std::vector<int32_t> newVoidStorageSlots,
	std::vector<ProtoComposite> newComposites,
	std::vector<ProtoOutputLink> newOutputLinks
)
{
	constexpr int32_t bigNumber = 10000;
	constantCount = newConstantValues.size();
	stacks = newStacks;
	workSlots = newWorkSlots;
	stackMaxCost = newStackMaxSize;
	storageSlots = newStorageSlots;
	storageSlotOverheadPenalty = newStorageSlotOverheadPenalty;
	workSlotOverheadPenalty = newWorkSlotOverheadPenalty;
	inputCount = newInputStorageSlots.size();
	compositeCount = newComposites.size();
	outputCount = newOutputLinks.size();
	CheckRange(stacks, 1, bigNumber);
	CheckRange(workSlots, 2, bigNumber);
	CheckRange(storageSlots, 1, bigNumber);
	CheckRange(constantCount, 0, bigNumber);
	CheckRange(inputCount, 1, bigNumber);
	CheckRange(compositeCount, 1, bigNumber);
	CheckRange(outputCount, 1, bigNumber);
	CheckRange(inputCount + constantCount, 0, storageSlots + 1);
	CheckRange(outputCount + constantCount, 0, storageSlots + 1);
	constantValues.resize(constantCount);
	nodes.resize(constantCount + inputCount + compositeCount + outputCount);
	auto link = [this](Node &node, int32_t sourceIndex, Link::LinkType linkType) {
		auto &source = sources[sourceIndex];
		source.uses += 1;
		auto nodeIndex = source.nodeIndex;
		auto &linkedNode = nodes[nodeIndex];
		auto linkIndex = links.size();
		Link &link = links.emplace_back();
		link.type = linkType;
		link.directions[linkUpstream].nodeIndex = nodeIndex;
		link.directions[linkDownstream].nodeIndex = &node - &nodes[0];
		link.directions[linkUpstream].linkIndicesIndex = linkedNode.linkIndices[linkDownstream].size();
		link.directions[linkDownstream].linkIndicesIndex = node.linkIndices[linkUpstream].size();
		link.upstreamOutputIndex = source.outputIndex;
		linkedNode.linkIndices[linkDownstream].push_back(linkIndex);
		node.linkIndices[linkUpstream].push_back(linkIndex);
	};
	auto presentSource = [this](int32_t nodeIndex, int32_t outputIndex) {
		nodes[nodeIndex].sources.push_back(int32_t(sources.size()));
		sources.push_back({ nodeIndex, outputIndex });
	};
	auto seenLsnsLife3 = false;
	for (int32_t constantIndex = 0; constantIndex < constantCount; ++constantIndex)
	{
		auto nodeIndex = constantIndex;
		auto &constant = nodes[nodeIndex];
		constant.type = Node::constant;
		auto &constantValue = constantValues[constantIndex];
		constantValue = newConstantValues[constantIndex];
		if (constantValue == lsnsLife3Value)
		{
			seenLsnsLife3 = true;
		}
		presentSource(nodeIndex, 0);
	}
	CheckRange(seenLsnsLife3 ? 1 : 0, 1, 2);
	inputStorageSlots.resize(inputCount);
	for (int32_t inputIndex = 0; inputIndex < inputCount; ++inputIndex)
	{
		auto nodeIndex = constantCount + inputIndex;
		auto &input = nodes[nodeIndex];
		input.type = Node::input;
		auto &inputStorageSlot = inputStorageSlots[inputIndex];
		inputStorageSlot = newInputStorageSlots[inputIndex];
		CheckRange(inputStorageSlot, 0, storageSlots);
		presentSource(nodeIndex, 0);
	}
	for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
	{
		auto &protoComposite = newComposites[compositeIndex];
		auto nodeIndex = constantCount + inputCount + compositeIndex;
		auto &node = nodes[nodeIndex];
		if (auto *protoSelect = std::get_if<ProtoSelect>(&protoComposite))
		{
			node.type = Node::select;
			auto stageCount = int32_t(protoSelect->tmps.size()) + 1;
			auto laneCount2 = int32_t(protoSelect->sources.size()) - stageCount;
			CheckRange(laneCount2, 2, bigNumber);
			CheckRange(laneCount2 % 2, 0, 1);
			auto laneCount = laneCount2 / 2;
			CheckRange(stageCount, 2, bigNumber);
			node.workSlotsNeeded = stageCount + laneCount * 2;
			CheckRange(node.workSlotsNeeded, 1, workSlots + 1);
			node.tmps.resize(stageCount - 1);
			for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
			{
				auto nonzeroSource = protoSelect->sources[laneIndex * 2];
				auto zeroSource = protoSelect->sources[laneIndex * 2 + 1];
				CheckRange(nonzeroSource, 0, sources.size());
				CheckRange(zeroSource, 0, sources.size());
				link(node, nonzeroSource, Link::toSelectNonzero);
				link(node, zeroSource, Link::toSelectZero);
			}
			for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
			{
				if (stageIndex > 0)
				{
					auto tmp = protoSelect->tmps[stageIndex - 1];
					CheckRange(tmp, 0, tmpCount);
					node.tmps[stageIndex - 1] = tmp;
				}
				auto source = protoSelect->sources[laneCount * 2 + stageIndex];
				CheckRange(source, 0, sources.size());
				link(node, source, Link::toBinary);
			}
			for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
			{
				presentSource(nodeIndex, laneIndex);
			}
		}
		else if (auto *protoBinary = std::get_if<ProtoBinary>(&protoComposite))
		{
			node.type = Node::binary;
			node.tmps.resize(1);
			node.tmps[0] = protoBinary->tmp;
			auto rhsSource = protoBinary->rhsSource;
			auto lhsSource = protoBinary->lhsSource;
			CheckRange(node.tmps[0], 0, tmpCount);
			CheckRange(rhsSource, 0, sources.size());
			CheckRange(lhsSource, 0, sources.size());
			link(node, rhsSource, Link::toBinary);
			link(node, lhsSource, Link::toBinary);
			node.workSlotsNeeded = 2;
			presentSource(nodeIndex, 0);
		}
	}
	outputLinks.resize(outputCount);
	for (int32_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
	{
		auto nodeIndex = constantCount + inputCount + compositeCount + outputIndex;
		auto &node = nodes[nodeIndex];
		auto &outputSource = outputLinks[outputIndex].sourceIndex;
		auto &outputStorageSlot = outputLinks[outputIndex].storageSlot;
		outputSource = newOutputLinks[outputIndex].source;
		outputStorageSlot = newOutputLinks[outputIndex].storageSlot;
		CheckRange(outputSource, 0, sources.size());
		CheckRange(outputStorageSlot, 0, storageSlots);
		node.type = Node::output;
		link(node, outputSource, Link::toOutput);
	}
	clobberStorageSlots.resize(newClobberStorageSlots.size());
	for (int32_t clobberIndex = 0; clobberIndex < int32_t(clobberStorageSlots.size()); ++clobberIndex)
	{
		clobberStorageSlots[clobberIndex] = newClobberStorageSlots[clobberIndex];
		CheckRange(clobberStorageSlots[clobberIndex], 0, storageSlots);
	}
	voidStorageSlots.resize(newVoidStorageSlots.size());
	for (int32_t voidIndex = 0; voidIndex < int32_t(voidStorageSlots.size()); ++voidIndex)
	{
		voidStorageSlots[voidIndex] = newVoidStorageSlots[voidIndex];
		CheckRange(voidStorageSlots[voidIndex], 0, storageSlots);
	}
}

namespace
{
	double TransitionProbability(double energy, double newEnergy, double temperature)
	{
		if (newEnergy < energy)
		{
			return 1.0;
		}
		return std::exp(-(newEnergy - energy) / temperature);
	}

	struct ThreadContext
	{
		std::mt19937_64 rng;
		std::thread thr;
		OptimizerState ostate;
		bool threadWorking = false;
		bool threadExit = false;
		std::mutex threadStateMx;
		std::condition_variable threadStateCv;

		void ThreadFunc(Optimizer::DispatchParameters dp)
		{
			while (true)
			{
				{
					std::unique_lock lk(threadStateMx);
					threadStateCv.wait(lk, [this]() {
						return threadWorking || threadExit;
					});
					if (threadExit)
					{
						break;
					}
				}
				ostate = OptimizeOnce(rng, ostate, dp.iterationCount);
				{
					std::unique_lock lk(threadStateMx);
					threadWorking = false;
				}
				threadStateCv.notify_all();
			}
		}

		void Start()
		{
			{
				std::unique_lock lk(threadStateMx);
				threadWorking = true;
			}
			threadStateCv.notify_all();
		}

		void Exit()
		{
			{
				std::unique_lock lk(threadStateMx);
				threadExit = true;
			}
			threadStateCv.notify_all();
		}

		void Wait()
		{
			std::unique_lock lk(threadStateMx);
			threadStateCv.wait(lk, [this]() {
				return !threadWorking;
			});
		}
	};
}

int64_t Schedule::NormalizeProgress(int64_t progress) const
{
	return std::max(std::min(progress, endsAt), int64_t(0));
}

bool OptimizerState::Done() const
{
	return progress >= schedule->endsAt;
}

double OptimizerState::Temperature() const
{
	auto it = std::lower_bound(schedule->steps.begin(), schedule->steps.end(), progress, [](auto &step, auto progress) {
		return step.beginsAt <= progress;
	});
	if (it == schedule->steps.end())
	{
		return schedule->steps.back().temperature;
	}
	auto &stepLeft = *(it - 1);
	auto &stepRight = *it;
	return stepLeft.temperature + (stepRight.temperature - stepLeft.temperature) * (progress - stepLeft.beginsAt) / (stepRight.beginsAt - stepLeft.beginsAt);
}

OptimizerState OptimizeOnce(std::mt19937_64 &rng, OptimizerState os, int32_t iterationCount)
{
	std::uniform_real_distribution<double> rdist(0.0, 1.0);
	auto energyLinear = os.state->GetEnergy<Energy>().linear;
	for (int32_t iterationIndex = 0; iterationIndex < iterationCount && !os.Done(); ++iterationIndex)
	{
		auto newState = os.state->RandomNeighbour(rng);
		auto newEnergyLinear = newState->GetEnergy<Energy>().linear;
		if (TransitionProbability(energyLinear, newEnergyLinear, os.Temperature()) >= rdist(rng))
		{
			os.state = newState;
			energyLinear = newEnergyLinear;
		}
		os.progress += 1;
	}
	return os;
}

void Optimizer::Dispatch(DispatchParameters dp)
{
	assert(!dispatched);
	ready = false;
	cancelRequest = false;
	dispatched = true;
	thr = std::thread([this, dp]() {
		std::vector<ThreadContext> threadContexts(threadCount);
		for (auto &threadContext : threadContexts)
		{
			threadContext.rng.seed(rng());
			threadContext.thr = std::thread([&threadContext, dp]() {
				threadContext.ThreadFunc(dp);
			});
		}
		int32_t currentRound = 0;
		while (true)
		{
			auto shouldExchange = currentRound == 0;
			if (dp.roundsPerExchange > 0)
			{
				shouldExchange = currentRound % dp.roundsPerExchange == 0;
			}
			if (shouldExchange)
			{
				auto stateSample = PeekState();
				for (auto &threadContext : threadContexts)
				{
					threadContext.ostate = stateSample;
				}
			}
			for (auto &threadContext : threadContexts)
			{
				threadContext.Start();
			}
			for (auto &threadContext : threadContexts)
			{
				threadContext.Wait();
			}
			OptimizerState newState;
			double stateLinear = 0;
			for (auto &threadContext : threadContexts)
			{
				auto threadStateLinear = threadContext.ostate.state->GetEnergy<Energy>().linear;
				if (!newState.state || stateLinear > threadStateLinear)
				{
					newState = threadContext.ostate;
					stateLinear = threadStateLinear;
				}
			}
			PokeState(newState);
			if (newState.Done())
			{
				break;
			}
			if (cancelRequest)
			{
				break;
			}
		}
		for (auto &threadContext : threadContexts)
		{
			threadContext.Exit();
			threadContext.thr.join();
		}
		ready = true;
	});
}

void Optimizer::Wait()
{
	if (dispatched)
	{
		thr.join();
		dispatched = false;
	}
}

void Optimizer::Cancel()
{
	cancelRequest = true;
	Wait();
}

Optimizer::~Optimizer()
{
	Cancel();
}

OptimizerState Optimizer::PeekState()
{
	std::shared_lock lk(stateMx);
	return heldState;
}

void Optimizer::PokeState(OptimizerState newState)
{
	std::unique_lock lk(stateMx);
	heldState = newState;
}
