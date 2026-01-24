#include "Energy.hpp"
#include "Common.hpp"
#include "Design.hpp"
#include "Plan.hpp"
#include <algorithm>
#include <cassert>

namespace Spaghetti::Optimize
{
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
			constantValue[design->inputStorageSlots[inputIndex]] = design->inputInitials[inputIndex];
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
}
