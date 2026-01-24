#include "StateHandle.hpp"
#include "Design.hpp"
#include "Energy.hpp"
#include "Plan.hpp"
#include "PlanValue.hpp"
#include "State.hpp"
#include <lua.hpp>
#include <sstream>

namespace Spaghetti::Optimize
{
	int StateHandle::New(lua_State *L, std::shared_ptr<State> state)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(lua_newuserdata(L, sizeof(StateHandle)));
		if (!stateHandle)
		{
			throw std::bad_alloc();
		}
		new(stateHandle) StateHandle();
		stateHandle->state = state;
		luaL_newmetatable(L, StateHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int StateHandle::Gc(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		stateHandle->~StateHandle();
		return 0;
	}

	int StateHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, StateHandle::mtName);
		return 1;
	}

	int StateHandle::Dump(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		std::ostringstream ss;
		ss << *stateHandle->state;
		auto str = ss.str();
		lua_pushlstring(L, str.c_str(), str.size());
		return 1;
	}

	int StateHandle::EnergyWrapper(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		const auto &state = *stateHandle->state;
		auto plan = state.GetEnergy<EnergyWithPlan>();
		lua_pushnumber(L, plan.linear);
		lua_pushinteger(L, plan.storageSlotCount);
		lua_pushinteger(L, plan.partCount);
		lua_newtable(L);
		{
			auto &steps = plan.GetSteps();
			auto &layers = state.GetLayers();
			auto *design = state.GetDesign();
			auto &nodes = design->Nodes();
			lua_newtable(L);
			auto storageSlotsIndex = lua_gettop(L);
			lua_newtable(L);
			auto workSlotsIndex = lua_gettop(L);
			auto showStorageSlots = std::max(plan.storageSlotCount, state.GetDesign()->StorageSlots());
			auto showWorkSlots = plan.workSlotCount;
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
			auto emitStorageSlots = [L, storageSlotsIndex](const std::vector<StorageSlot> &storageSlots) {
				lua_newtable(L);
				for (int32_t storageSlotIndex = 0; storageSlotIndex < int32_t(storageSlots.size()); ++storageSlotIndex)
				{
					auto &storageSlot = storageSlots[storageSlotIndex];
					if (storageSlot.usesLeft)
					{
						lua_pushinteger(L, storageSlot.sourceIndex + 1);
					}
					else
					{
						lua_pushnil(L);
					}
					lua_rawseti(L, -2, storageSlotIndex + 1);
				}
				lua_rawseti(L, storageSlotsIndex, lua_objlen(L, storageSlotsIndex) + 1);
			};
			while (true)
			{
				auto &step = steps[planIndex];
				planIndex += 1;
				if (std::get_if<EnergyWithPlan::Commit>(&step))
				{
					break;
				}
				handleStoragePlanStep(step);
			}
			for (int32_t layerIndex = 1; layerIndex < int32_t(layers.size()) - 1; ++layerIndex)
			{
				auto storageSlotsCopy = storageSlots;
				struct WorkSlotState
				{
					std::optional<int32_t> nodeIndex;
				};
				std::vector<WorkSlotState> workSlotStates(showWorkSlots);
				while (true)
				{
					auto &step = steps[planIndex];
					planIndex += 1;
					if (std::get_if<EnergyWithPlan::Commit>(&step))
					{
						break;
					}
					else if (auto *load = std::get_if<EnergyWithPlan::Load>(&step))
					{
						workSlotStates[load->workSlot].nodeIndex = load->nodeIndex;
					}
					else if (auto *cload = std::get_if<EnergyWithPlan::Cload>(&step))
					{
						workSlotStates[cload->workSlot].nodeIndex = cload->nodeIndex;
					}
					handleStoragePlanStep(step);
				}
				emitStorageSlots(storageSlotsCopy);
				lua_newtable(L);
				for (int32_t workSlotIndex = 0; workSlotIndex < int32_t(workSlotStates.size()); ++workSlotIndex)
				{
					auto &workSlotState = workSlotStates[workSlotIndex];
					if (workSlotState.nodeIndex)
					{
						lua_pushinteger(L, nodes[*workSlotState.nodeIndex].sources[0] + 1);
					}
					else
					{
						lua_pushnil(L);
					}
					lua_rawseti(L, -2, workSlotIndex + 1);
				}
				lua_rawseti(L, workSlotsIndex, lua_objlen(L, workSlotsIndex) + 1);
			}
			emitStorageSlots(storageSlots);
			lua_setfield(L, -3, "work_slot_states");
			lua_setfield(L, -2, "storage_slot_states");
			lua_pushinteger(L, showWorkSlots);
			lua_setfield(L, -2, "work_slots");
			lua_pushinteger(L, showStorageSlots);
			lua_setfield(L, -2, "storage_slots");
		}
		lua_newtable(L);
		{
			auto &voids = stateHandle->state->GetDesign()->VoidStorageSlots();
			for (int32_t voidIndex = 0; voidIndex < int32_t(voids.size()); ++voidIndex)
			{
				lua_pushinteger(L, voids[voidIndex] + 1);
				lua_rawseti(L, -2, voidIndex + 1);
			}
		}
		return 5;
	}

	int StateHandle::PlanWrapper(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		std::shared_ptr<Plan> plan;
		try
		{
			plan = stateHandle->state->GetEnergy<EnergyWithPlan>().ToPlan();
		}
		catch (const EnergyWithPlan::ToPlanFailed &ex)
		{
			lua_pushnil(L);
			lua_pushfstring(L, "design parameters not satisfied, no plan generated: %s", ex.what());
			return 2;
		}
		auto setField = [L](const char *k, int32_t v) {
			lua_pushinteger(L, v);
			lua_setfield(L, -2, k);
		};
		auto setType = [L](const char *v) {
			lua_pushstring(L, v);
			lua_setfield(L, -2, "type");
		};
		lua_newtable(L);
		luaL_newmetatable(L, PlanValue::mtName);
		lua_setmetatable(L, -2);
		setField("stacks", stateHandle->state->GetDesign()->Stacks());
		setField("stacks_used", plan->stacksUsed);
		setField("part_count", plan->cost);
		lua_newtable(L);
		for (int32_t stepIndex = 0; stepIndex < int32_t(plan->steps.size()); ++stepIndex)
		{
			auto &step = plan->steps[stepIndex];
			lua_newtable(L);
			if (auto *load = std::get_if<Plan::Load>(&step))
			{
				setType("load");
				setField("stack_index", load->stackIndex);
				setField("work_slot", load->workSlot);
				setField("storage_slot", load->storageSlot);
			}
			else if (auto *cload = std::get_if<Plan::Cload>(&step))
			{
				setType("cload");
				setField("stack_index", cload->stackIndex);
				setField("work_slot", cload->workSlot);
			}
			else if (auto *mode = std::get_if<Plan::Mode>(&step))
			{
				setType("mode");
				setField("stack_index", mode->stackIndex);
				setField("tmp", mode->tmp);
			}
			else if (auto *store = std::get_if<Plan::Store>(&step))
			{
				setType("store");
				setField("stack_index", store->stackIndex);
				setField("work_slot", store->workSlot);
				setField("storage_slot", store->storageSlot);
			}
			else if (auto *cstore = std::get_if<Plan::Cstore>(&step))
			{
				setType("cstore");
				setField("stack_index", cstore->stackIndex);
				setField("work_slot", cstore->workSlot);
				setField("storage_slot", cstore->storageSlot);
			}
			else if (auto *aray = std::get_if<Plan::Aray>(&step))
			{
				setType("aray");
				setField("stack_index", aray->stackIndex);
			}
			else if (auto *east = std::get_if<Plan::East>(&step))
			{
				setType("east");
				setField("stack_index", east->stackIndex);
			}
			else if (auto *west = std::get_if<Plan::West>(&step))
			{
				setType("west");
				setField("stack_index", west->stackIndex);
			}
			else if (auto *clear = std::get_if<Plan::Clear>(&step))
			{
				setType("clear");
				setField("stack_index", clear->stackIndex);
			}
			else if (auto *top = std::get_if<Plan::Top>(&step))
			{
				setType("top");
				setField("stack_index", top->stackIndex);
			}
			else if (auto *bottom = std::get_if<Plan::Bottom>(&step))
			{
				setType("bottom");
				setField("stack_index", bottom->stackIndex);
			}
			else if (auto *lcap = std::get_if<Plan::Lcap>(&step))
			{
				setType("lcap");
				setField("life3_index", lcap->life3Index);
			}
			else if (auto *lfilt = std::get_if<Plan::Lfilt>(&step))
			{
				setType("lfilt");
				setField("work_slot", lfilt->workSlot);
			}
			else if (auto *rfilt = std::get_if<Plan::Rfilt>(&step))
			{
				setType("rfilt");
				setField("storage_slot", rfilt->storageSlot);
				setField("constant_value", rfilt->constantValue);
			}
			lua_rawseti(L, -2, stepIndex + 1);
		}
		lua_setfield(L, -2, "steps");
		return 1;
	}

	void StateHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg stateReg[] = {
			{ "dump"  , Dump          },
			{ "energy", EnergyWrapper },
			{ "plan"  , PlanWrapper   },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg stateMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, stateMt);
		lua_newtable(L);
		luaL_register(L, NULL, stateReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "state_mt");
		lua_pop(L, 1);
	}
}
