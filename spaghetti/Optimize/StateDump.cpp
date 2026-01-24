#include "Design.hpp"
#include "Energy.hpp"
#include "State.hpp"
#include <iomanip>

namespace Spaghetti::Optimize
{
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
}
