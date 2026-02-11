#include "State.hpp"
#include "Common.hpp"
#include "Design.hpp"
#include "Energy.hpp"
#include "Plan.hpp"
#include <cassert>

namespace Spaghetti::Optimize
{
	template<class EnergyType>
	EnergyType State::GetEnergy() const
	{
		constexpr auto withPlan = std::is_same_v<EnergyType, EnergyWithPlan>;
		EnergyType energy;
		struct OutputRemap
		{
			int32_t from, to;
		};
		std::vector<OutputRemap> outputRemaps;
		struct OutputWorkRemap
		{
			int32_t from, to;
		};
		std::vector<OutputWorkRemap> outputWorkRemaps;
		auto nodeIndexToLayerIndex = NodeIndexToLayerIndex();
		struct Storage
		{
			int32_t usesLeft = 0;
			int32_t slotIndex = -1;
			std::vector<int32_t> outputLinks;
			std::vector<int32_t> workOutputLinks;
		};
		std::vector<std::optional<int32_t>> slots;
		std::vector<Storage> storage(design->sources.size());
		std::vector<int32_t> disallowConstantsInSlots(design->storageSlots, 0); // std::vector<bool> is stupid
		for (auto &outputLink : design->outputLinks)
		{
			if (auto *storeSlotOutputLink = std::get_if<Design::StorageSlotOutputLink>(&outputLink))
			{
				storage[storeSlotOutputLink->sourceIndex].outputLinks.push_back(storeSlotOutputLink->storageSlot);
				disallowConstantsInSlots[storeSlotOutputLink->storageSlot] = 1;
			}
			if (auto *workSlotOutputLink = std::get_if<Design::WorkSlotOutputLink>(&outputLink))
			{
				storage[workSlotOutputLink->sourceIndex].workOutputLinks.push_back(workSlotOutputLink->workSlot);
			}
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
			&outputWorkRemaps,
			&energy,
			&storage,
			&disallowConstantsInSlots,
			&slots
		](int32_t layerIndex, int32_t sourceIndex, StorageUsage usage, std::optional<int32_t> freeSlotIndex, int32_t uses) {
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
				for (auto slotIndex : storage[sourceIndex].workOutputLinks)
				{
					outputWorkRemaps.push_back({ *freeSlotIndex, slotIndex });
				}
				storage[sourceIndex].usesLeft = uses;
				storage[sourceIndex].slotIndex = *freeSlotIndex;
				if constexpr (withPlan)
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
			if constexpr (withPlan)
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
			allocStorage(0, sourceIndex, usageNormal, design->inputStorageSlots[inputIndex], design->sources[sourceIndex].uses);
		}
		for (int32_t voidIndex = 0; voidIndex < int32_t(design->voidStorageSlots.size()); ++voidIndex)
		{
			allocStorage(0, -1, usageVoid, design->voidStorageSlots[voidIndex], -1 /* voids don't care about uses left */);
		}
		for (int32_t constantIndex = 0; constantIndex < design->constantCount; ++constantIndex)
		{
			auto nodeIndex = constantIndex;
			auto &node = design->nodes[nodeIndex];
			auto sourceIndex = node.sources[0];
			auto storageSlotIndex = allocStorage(0, sourceIndex, usageConstant, std::nullopt, -1 /* constants have infinite uses */);
			if constexpr (withPlan)
			{
				energy.steps.push_back(EnergyWithPlan::Constant{ { 0 }, storageSlotIndex, design->constantValues[constantIndex] });
			}
		}
		if constexpr (withPlan)
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
				int32_t uses;
			};
			std::vector<StoreScheduleEntry> storeSchedule;
			auto toSelectZeroLinkToSourceIndex = [this](const Link &link) {
				auto &node = design->nodes[link.neighbours[Link::directionDown].nodeIndex];
				auto laneIndex = (link.neighbours[Link::directionDown].linkIndicesIndex - 1) / 2;
				return std::pair<int32_t, int32_t>{ laneIndex, node.sources[laneIndex] };
			};
			auto doStore = [this, &storeSchedule](int32_t workSlotIndex, int32_t sourceIndex, int32_t uses) {
				auto storeScheduleIndex = int32_t(storeSchedule.size());
				assert(uses);
				assert(design->sources[sourceIndex].uses == uses ||
				       design->sources[sourceIndex].uses == uses + 1);
				storeSchedule.push_back({ sourceIndex, workSlotIndex, {}, uses });
				return storeScheduleIndex;
			};
			std::vector<int32_t> selectStorageSlotSchedule;
			auto doCstore = [this, &storeSchedule, &toSelectZeroLinkToSourceIndex, &selectStorageSlotSchedule](int32_t workSlotIndex, const Link &link) {
				auto storeScheduleIndex = int32_t(storeSchedule.size());
				auto [ laneIndex, sourceIndex ] = toSelectZeroLinkToSourceIndex(link);
				storeSchedule.push_back({ sourceIndex, -1, workSlotIndex, design->sources[sourceIndex].uses });
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
					if constexpr (withPlan)
					{
						energy.steps.push_back(EnergyWithPlan::Mode{ { layerIndex }, workSlotIndex, tmp });
					}
					tmpLoads[tmp].used = true;
				}
				if (tmpLoads[tmp].slotUsed[storageSlotIndex])
				{
					energy.partCount += Plan::Cload::cost;
					if constexpr (withPlan)
					{
						energy.steps.push_back(EnergyWithPlan::Cload{ { layerIndex }, nodeIndex, tmp, workSlotIndex, storageSlotIndex });
					}
				}
				else
				{
					tmpLoads[tmp].slotUsed[storageSlotIndex] = 1;
					energy.partCount += Plan::Load::cost;
					if constexpr (withPlan)
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
				auto linkIndex = node.linkIndices[Link::directionUp][linkIndicesIndex];
				auto &link = design->links[linkIndex];
				auto linkedNodeIndex = link.neighbours[Link::directionUp].nodeIndex;
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
						auto linkIndexNext = node.linkIndices[Link::directionUp][linkIndicesIndex + 1];
						auto &linkNext = design->links[linkIndexNext];
						auto linkedNodeNextIndex = linkNext.neighbours[Link::directionUp].nodeIndex;
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
					}
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
					}
					for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
					{
						doLinkUpstream(nodeIndex, laneIndex * 2);
						doCstoreStore(workSlotsUsed - 1, selectStorageSlotSchedule[laneIndex]);
					}
				}
				else
				{
					for (int32_t linkIndicesIndex = 0; linkIndicesIndex < int32_t(node.linkIndices[Link::directionUp].size()); ++linkIndicesIndex)
					{
						doLinkUpstream(nodeIndex, linkIndicesIndex);
					}
					bool needsStore = false;
					int32_t sameLayerLinks = 0;
					for (auto linkIndex : node.linkIndices[Link::directionDown])
					{
						auto &link = design->links[linkIndex];
						auto linkedNodeIndex = link.neighbours[Link::directionDown].nodeIndex;
						if (nodeIndexToLayerIndex[linkedNodeIndex] != layerIndex)
						{
							needsStore = true;
						}
						if (nodeIndexToLayerIndex[linkedNodeIndex] == layerIndex && link.type == Link::toBinary)
						{
							sameLayerLinks += 1;
						}
						if (nodeIndexToLayerIndex[linkedNodeIndex] == layerIndex && link.type == Link::toSelectZero)
						{
							doCstore(workSlotsUsed - 1, link);
						}
					}
					if (needsStore)
					{
						doStore(workSlotsUsed - 1, node.sources[0], design->sources[node.sources[0]].uses - sameLayerLinks);
					}
				}
			}
			for (auto &storeScheduleEntry : storeSchedule)
			{
				auto storageSlotIndex = allocStorage(layerIndex, storeScheduleEntry.sourceIndex, usageNormal, std::nullopt, storeScheduleEntry.uses);
				energy.partCount += Plan::Store::cost;
				if constexpr (withPlan)
				{
					energy.steps.push_back(EnergyWithPlan::Store{ { layerIndex }, storeScheduleEntry.workSlotIndex, storageSlotIndex });
				}
				if (storeScheduleEntry.cworkSlotIndex)
				{
					energy.partCount += Plan::Cstore::cost;
					if constexpr (withPlan)
					{
						energy.steps.push_back(EnergyWithPlan::Cstore{ { layerIndex }, *storeScheduleEntry.cworkSlotIndex, storageSlotIndex });
					}
				}
			}
			energy.partCount += Plan::commitCost;
			if constexpr (withPlan)
			{
				energy.steps.push_back(EnergyWithPlan::Commit{ layerIndex });
			}
		}
		auto storageSlotCount = int32_t(slots.size());
		auto storageSlotOverhead = std::max(0, storageSlotCount - design->storageSlots);
		auto workSlotCount = int32_t(outputRemaps.size());
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
		if constexpr (withPlan)
		{
			if (outputRemaps.size() || outputWorkRemaps.size())
			{
				std::vector<int32_t> outputWorkRemapsFrom(design->workSlots, -1);
				for (int32_t outputWorkRemapIndex = 0; outputWorkRemapIndex < int(outputWorkRemaps.size()); ++outputWorkRemapIndex)
				{
					auto &outputWorkRemap = outputWorkRemaps[outputWorkRemapIndex];
					outputWorkRemapsFrom[outputWorkRemap.to] = outputWorkRemap.from;
				}
				auto layerIndex = int32_t(layers.size()) - 1;
				energy.steps.push_back(EnergyWithPlan::Mode{ { layerIndex }, 0, 0 });
				int32_t outputRemapIndex = 0;
				for (int32_t workSlotIndex = 0; workSlotIndex < design->workSlots; ++workSlotIndex)
				{
					if (outputWorkRemapsFrom[workSlotIndex] != -1)
					{
						energy.steps.push_back(EnergyWithPlan::Load{ { layerIndex }, -1, 0, workSlotIndex, outputWorkRemapsFrom[workSlotIndex] });
					}
					else if (outputRemapIndex < int32_t(outputRemaps.size()))
					{
						auto &outputRemap = outputRemaps[outputRemapIndex];
						energy.steps.push_back(EnergyWithPlan::Load{ { layerIndex }, -1, 0, workSlotIndex, outputRemap.from });
						energy.steps.push_back(EnergyWithPlan::Store{ { layerIndex }, workSlotIndex, outputRemap.to });
						outputRemapIndex += 1;
					}
				}
				energy.steps.push_back(EnergyWithPlan::Commit{ layerIndex });
			}
			energy.SortSteps();
		}
		return energy;
	}

	template Energy State::GetEnergy<Energy>() const;
	template EnergyWithPlan State::GetEnergy<EnergyWithPlan>() const;
}
