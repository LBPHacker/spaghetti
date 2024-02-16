#include "optimize.hpp"
#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <iomanip>
#include <iterator>
#include <mutex>

#include <iostream>

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

	struct CheckStream
	{
	};
	std::istream &operator >>(std::istream &stream, const CheckStream &checkCin)
	{
		if (!stream)
		{
			throw StreamFailed("stream failed");
		}
		return stream;
	}
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

std::vector<Move> State::ValidMoves() const
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
			// make sure we can move it to an existing layer
			if (!(newLayerIndex2 & 1) && !bool(design->CheckLayer(InsertNode(int32_t(newLayerIndex2 / 2), nodeIndex))))
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

std::shared_ptr<State> State::RandomNeighbour(std::mt19937_64 &rng) const
{
	auto moves = ValidMoves();
	if (!moves.size())
	{
		return std::make_shared<State>(*this);
	}
	auto move = moves[rng() % moves.size()];
	auto neighbour = std::make_shared<State>();
	neighbour->iteration = iteration + 1;
	neighbour->design = design;
	auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
	for (int32_t layerIndex2 = 0; layerIndex2 < int32_t(layers.size()) * 2; ++layerIndex2)
	{
		if (layerIndex2 & 1)
		{
			if (layerIndex2 == move.layerIndex2)
			{
				neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
				neighbour->nodeIndices.push_back(move.nodeIndex);
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
					neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
					for (auto nodeIndicesIndex = layerBegin; nodeIndicesIndex < layerEnd; ++nodeIndicesIndex)
					{
						auto nodeIndex = nodeIndices[nodeIndicesIndex];
						if (nodeIndex != move.nodeIndex)
						{
							neighbour->nodeIndices.push_back(nodeIndex);
						}
					}
				}
			}
			else
			{
				neighbour->layers.push_back(int32_t(neighbour->nodeIndices.size()));
				if (layerIndex2 == move.layerIndex2)
				{
					auto nodeIndicesCopy = InsertNode(layerIndex, move.nodeIndex);
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
				if (auto *load = std::get_if<EnergyWithPlan::Load>(&step))
				{
					return { -load->tmp, 1, load->storageSlot, 0 };
				}
				else if (auto *cload = std::get_if<EnergyWithPlan::Cload>(&step))
				{
					return { -cload->tmp, 1, cload->storageSlot, 1 };
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
	constexpr int32_t stackMaxCost       = 1495;
	constexpr auto    bottomTopCost      = Plan::Bottom::cost + Plan::Top::cost;
	constexpr auto    stackLayersMaxCost = stackMaxCost - bottomTopCost;
	auto plan = std::make_shared<Plan>();
	int32_t lsnsLife3Index = -1;
	std::vector<int32_t> constantValue(design->storageSlots, 0);
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
	auto flushStack = [&stackBuffer, &plan, &stackIndex]() {
		if (stackBuffer.cost)
		{
			plan->steps.push_back(Plan::Bottom{ stackIndex });
			plan->steps.insert(plan->steps.end(), stackBuffer.steps.begin(), stackBuffer.steps.end());
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
	auto flushLayer = [&stackIndex, &beganStore, &layerOpen, &pushToLayer, &stackBuffer, &layerBuffer, &flushStack]() {
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
	for (int32_t storageSlotIndex = 0; storageSlotIndex < design->storageSlots; ++storageSlotIndex)
	{
		plan->steps.push_back(Plan::Rfilt{ { stackIndex }, storageSlotIndex, constantValue[storageSlotIndex] });
	}
	for (int32_t workSlotIndex = 0; workSlotIndex < design->workSlots; ++workSlotIndex)
	{
		plan->steps.push_back(Plan::Lfilt{ { stackIndex }, workSlotIndex });
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
			pushToLayer(Plan::Load{ { stackIndex }, load->workSlot, load->storageSlot });
		}
		else if (auto *cload = std::get_if<Cload>(&step))
		{
			beginLayer();
			pushToLayer(Plan::Cload{ { stackIndex }, cload->workSlot });
		}
		else if (auto *store = std::get_if<Store>(&step))
		{
			beginStore();
			pushToLayer(Plan::Store{ { stackIndex }, store->workSlot, store->storageSlot });
		}
		else if (auto *cstore = std::get_if<Cstore>(&step))
		{
			beginStore();
			pushToLayer(Plan::Cstore{ { stackIndex }, cstore->workSlot, cstore->storageSlot });
		}
		else if (auto *mode = std::get_if<Mode>(&step))
		{
			beginLayer();
			pushToLayer(Plan::Mode{ { stackIndex }, mode->tmp });
		}
	}
	flushStack();
	for (auto &step : plan->steps)
	{
		std::visit([&plan](auto &step) {
			plan->cost += step.cost;
		}, step);
	}
	plan->stackCount = stackIndex;
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
	auto allocStorage = [
		this,
		&outputRemaps,
		&energy,
		&storage,
		&disallowConstantsInSlots,
		&slots
	](int32_t layerIndex, int32_t sourceIndex, bool forConstant, std::optional<int32_t> freeSlotIndex) {
		for (auto slotIndex : storage[sourceIndex].outputLinks)
		{
			if (!*freeSlotIndex && !slots[slotIndex])
			{
				freeSlotIndex = slotIndex;
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
		auto slotOk = [&slots, &disallowConstantsInSlots, forConstant](int32_t slotIndex) {
			return !slots[slotIndex] && !(forConstant && slotIndex < int32_t(disallowConstantsInSlots.size()) && disallowConstantsInSlots[slotIndex]);
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
		for (auto slotIndex : storage[sourceIndex].outputLinks)
		{
			if (slotIndex != *freeSlotIndex)
			{
				outputRemaps.push_back({ *freeSlotIndex, slotIndex });
			}
		}
		auto uses = design->sources[sourceIndex].uses;
		if (forConstant)
		{
			uses = -1; // constants have infinite uses
		}
		storage[sourceIndex].usesLeft = uses;
		storage[sourceIndex].slotIndex = *freeSlotIndex;
		if constexpr (std::is_same_v<EnergyType, EnergyWithPlan>)
		{
			energy.steps.push_back(EnergyWithPlan::AllocStorage{ { layerIndex }, sourceIndex, *freeSlotIndex, uses });
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
		allocStorage(0, sourceIndex, false, design->inputStorageSlots[inputIndex]);
	}
	for (int32_t constantIndex = 0; constantIndex < design->constantCount; ++constantIndex)
	{
		auto nodeIndex = constantIndex;
		auto &node = design->nodes[nodeIndex];
		auto sourceIndex = node.sources[0];
		auto storageSlotIndex = allocStorage(0, sourceIndex, true, std::nullopt);
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
			storeSchedule.push_back({ sourceIndex, workSlotIndex });
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
			auto storageSlotIndex = allocStorage(layerIndex, storeScheduleEntry.sourceIndex, false, std::nullopt);
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
	energy.linear = double(energy.partCount) + double(storageSlotOverhead) * design->storageSlotOverheadPenalty;
	energy.storageSlotCount = storageSlotCount;
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

std::istream &operator >>(std::istream &stream, Design &design)
{
	int32_t workSlots;
	int32_t storageSlots;
	double storageSlotOverheadPenalty;
	int32_t constantCount;
	int32_t inputCount;
	int32_t compositeCount;
	int32_t outputCount;
	int32_t clobberCount;
	stream >> workSlots >> storageSlots >> storageSlotOverheadPenalty >> constantCount >> inputCount >> compositeCount >> outputCount >> clobberCount >> CheckStream();
	std::vector<int32_t> constantValues(constantCount);
	for (int32_t constantIndex = 0; constantIndex < constantCount; ++constantIndex)
	{
		stream >> constantValues[constantIndex] >> CheckStream();
	}
	std::vector<int32_t> inputStorageSlots(inputCount);
	for (int32_t inputIndex = 0; inputIndex < inputCount; ++inputIndex)
	{
		stream >> inputStorageSlots[inputIndex] >> CheckStream();
	}
	std::vector<Design::ProtoComposite> composites(compositeCount);
	for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
	{
		int32_t tmp;
		stream >> tmp >> CheckStream();
		CheckRange(tmp, 0, tmpCount + 1);
		if (tmp == tmpCount)
		{
			Design::ProtoSelect select;
			int32_t laneCount;
			int32_t stageCount;
			stream >> laneCount >> stageCount >> CheckStream();
			select.tmps.resize(stageCount - 1);
			select.sources.resize(laneCount * 2 + stageCount);
			for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
			{
				stream >> select.sources[laneIndex * 2] >> select.sources[laneIndex * 2 + 1] >> CheckStream();
			}
			for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
			{
				if (stageIndex > 0)
				{
					stream >> select.tmps[stageIndex - 1] >> CheckStream();
				}
				stream >> select.sources[laneCount * 2 + stageIndex] >> CheckStream();
			}
			composites[compositeIndex] = select;
		}
		else
		{
			Design::ProtoBinary binary;
			binary.tmp = tmp;
			stream >> binary.rhsSource >> binary.lhsSource >> CheckStream();
			composites[compositeIndex] = binary;
		}
	}
	std::vector<Design::ProtoOutputLink> outputLinks(outputCount);
	for (int32_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
	{
		auto &outputLink = outputLinks[outputIndex];
		stream >> outputLink.source >> outputLink.storageSlot >> CheckStream();
	}
	std::vector<int32_t> clobberStorageSlots(clobberCount);
	for (int32_t clobberIndex = 0; clobberIndex < clobberCount; ++clobberIndex)
	{
		stream >> clobberStorageSlots[clobberIndex] >> CheckStream();
	}
	design = Design(
		workSlots,
		storageSlots,
		storageSlotOverheadPenalty,
		constantValues,
		inputStorageSlots,
		clobberStorageSlots,
		composites,
		outputLinks
	);
	return stream;
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

std::ostream &operator <<(std::ostream &stream, const Plan &plan)
{
	stream << plan.stackCount << " " << plan.steps.size() << " " << plan.cost << std::endl;
	for (auto &step : plan.steps)
	{
		std::visit([&stream](auto &step) {
			stream << step.stackIndex << " ";
		}, step);
		stream << step.index();
		if (auto *load = std::get_if<Plan::Load>(&step))
		{
			stream << " " << load->workSlot << " " << load->storageSlot;
		}
		else if (auto *cload = std::get_if<Plan::Cload>(&step))
		{
			stream << " " << cload->workSlot;
		}
		else if (auto *mode = std::get_if<Plan::Mode>(&step))
		{
			stream << " " << mode->tmp;
		}
		else if (auto *store = std::get_if<Plan::Store>(&step))
		{
			stream << " " << store->workSlot << " " << store->storageSlot;
		}
		else if (auto *cstore = std::get_if<Plan::Cstore>(&step))
		{
			stream << " " << cstore->workSlot << " " << cstore->storageSlot;
		}
		else if (auto *lcap = std::get_if<Plan::Lcap>(&step))
		{
			stream << " " << lcap->life3Index;
		}
		else if (auto *lfilt = std::get_if<Plan::Lfilt>(&step))
		{
			stream << " " << lfilt->workSlot;
		}
		else if (auto *rfilt = std::get_if<Plan::Rfilt>(&step))
		{
			stream << " " << rfilt->storageSlot << " " << rfilt->constantValue;
		}
		stream << std::endl;
	}
	return stream;
}

std::optional<Design::CheckResult> Design::CheckLayer(const std::vector<int32_t> &nodeIndices) const
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
	if (checkResult.workSlots <= workSlots)
	{
		return checkResult;
	}
	// needs too many work slots
	return std::nullopt;
}

Design::Design(
	int32_t newWorkSlots,
	int32_t newStorageSlots,
	double newStorageSlotOverheadPenalty,
	std::vector<int32_t> newConstantValues,
	std::vector<int32_t> newInputStorageSlots,
	std::vector<int32_t> newClobberStorageSlots,
	std::vector<ProtoComposite> newComposites,
	std::vector<ProtoOutputLink> newOutputLinks
)
{
	constexpr int32_t bigNumber = 10000;
	constantCount = newConstantValues.size();
	workSlots = newWorkSlots;
	storageSlots = newStorageSlots;
	storageSlotOverheadPenalty = newStorageSlotOverheadPenalty;
	inputCount = newInputStorageSlots.size();
	compositeCount = newComposites.size();
	outputCount = newOutputLinks.size();
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
				OptimizeParameters op;
				op.temperatureInitial = ostate.temperature;
				op.iterationCount     = dp.iterationCount;
				op.temperatureFinal   = dp.temperatureFinal;
				op.temperatureLoss    = dp.temperatureLoss;
				ostate = OptimizeOnce(rng, *ostate.state, op);
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

OptimizerState OptimizeOnce(std::mt19937_64 &rng, const State &stateIn, OptimizeParameters op)
{
	auto state = std::make_shared<State>(stateIn);
	std::uniform_real_distribution<double> rdist(0.0, 1.0);
	auto temperature = op.temperatureInitial;
	for (int32_t iterationIndex = 0; iterationIndex < op.iterationCount && temperature > op.temperatureFinal; ++iterationIndex)
	{
		auto newState = state->RandomNeighbour(rng);
		auto energyLinear = state->GetEnergy<Energy>().linear;
		auto newEnergyLinear = newState->GetEnergy<Energy>().linear;
		if (TransitionProbability(energyLinear, newEnergyLinear, temperature) >= rdist(rng))
		{
			state = newState;
		}
		temperature -= op.temperatureLoss;
	}
	return { state, temperature };
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
		while (true)
		{
			auto stateSample = PeekState();
			if (!(stateSample.temperature > dp.temperatureFinal))
			{
				break;
			}
			for (auto &threadContext : threadContexts)
			{
				threadContext.ostate = stateSample;
				threadContext.Start();
			}
			for (auto &threadContext : threadContexts)
			{
				threadContext.Wait();
			}
			if (threadContexts.size())
			{
				stateSample.temperature = threadContexts[0].ostate.temperature;
			}
			auto stateLinear = stateSample.state->GetEnergy<Energy>().linear;
			for (auto &threadContext : threadContexts)
			{
				auto threadStateLinear = threadContext.ostate.state->GetEnergy<Energy>().linear;
				if (stateLinear > threadStateLinear)
				{
					stateSample.state = threadContext.ostate.state;
					stateLinear = threadStateLinear;
				}
			}
			PokeState(stateSample);
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
