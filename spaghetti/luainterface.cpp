#include "optimize.hpp"
#include <sstream>
#include <lua.hpp>

namespace
{
	struct StateHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.state";
		std::shared_ptr<State> state;

		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Dump(lua_State *L);
		static int EnergyWrapper(lua_State *L);
		static int PlanWrapper(lua_State *L);
	};

	struct DesignHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.design";
		std::shared_ptr<Design> design;

		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Initial(lua_State *L);
	};

	struct OptimizerHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.optimizer";
		std::shared_ptr<Optimizer> optimizer;

		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Wait(lua_State *L);
		static int Cancel(lua_State *L);
		static int StateWrapper(lua_State *L);
		static int Ready(lua_State *L);
		static int Dispatched(lua_State *L);
		static int Dispatch(lua_State *L);
	};

	int MakeStateHandle(lua_State *L, std::shared_ptr<State> state)
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

	int DesignHandle::Initial(lua_State *L)
	{
		auto *designHandle = reinterpret_cast<DesignHandle *>(luaL_checkudata(L, 1, DesignHandle::mtName));
		return MakeStateHandle(L, designHandle->design->Initial());
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
		auto energy = stateHandle->state->GetEnergy<Energy>();
		lua_pushnumber(L, energy.linear);
		lua_pushinteger(L, energy.storageSlotCount);
		lua_pushinteger(L, energy.partCount);
		return 3;
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
			return luaL_error(L, "design parameters not satisfied, no plan generated: %s", ex.what());
		}
		auto setField = [L](const char *k, int32_t v) {
			lua_pushinteger(L, v);
			lua_setfield(L, -2, k);
		};
		lua_newtable(L);
		setField("stack_count", plan->stackCount);
		setField("part_count", plan->cost);
		lua_newtable(L);
		for (int32_t stepIndex = 0; stepIndex < int32_t(plan->steps.size()); ++stepIndex)
		{
			auto &step = plan->steps[stepIndex];
			lua_newtable(L);
			if (auto *load = std::get_if<Plan::Load>(&step))
			{
				setField("stack_index", load->stackIndex);
				setField("work_slot", load->workSlot);
				setField("storage_slot", load->storageSlot);
			}
			else if (auto *cload = std::get_if<Plan::Cload>(&step))
			{
				setField("stack_index", cload->stackIndex);
				setField("work_slot", cload->workSlot);
			}
			else if (auto *mode = std::get_if<Plan::Mode>(&step))
			{
				setField("stack_index", mode->stackIndex);
				setField("tmp", mode->tmp);
			}
			else if (auto *store = std::get_if<Plan::Store>(&step))
			{
				setField("stack_index", store->stackIndex);
				setField("work_slot", store->workSlot);
				setField("storage_slot", store->storageSlot);
			}
			else if (auto *cstore = std::get_if<Plan::Cstore>(&step))
			{
				setField("stack_index", cstore->stackIndex);
				setField("work_slot", cstore->workSlot);
				setField("storage_slot", cstore->storageSlot);
			}
			else if (auto *aray = std::get_if<Plan::Aray>(&step))
			{
				setField("stack_index", aray->stackIndex);
			}
			else if (auto *east = std::get_if<Plan::East>(&step))
			{
				setField("stack_index", east->stackIndex);
			}
			else if (auto *west = std::get_if<Plan::West>(&step))
			{
				setField("stack_index", west->stackIndex);
			}
			else if (auto *clear = std::get_if<Plan::Clear>(&step))
			{
				setField("stack_index", clear->stackIndex);
			}
			else if (auto *top = std::get_if<Plan::Top>(&step))
			{
				setField("stack_index", top->stackIndex);
			}
			else if (auto *bottom = std::get_if<Plan::Bottom>(&step))
			{
				setField("stack_index", bottom->stackIndex);
			}
			else if (auto *lcap = std::get_if<Plan::Lcap>(&step))
			{
				setField("life3_index", lcap->life3Index);
			}
			else if (auto *lfilt = std::get_if<Plan::Lfilt>(&step))
			{
				setField("work_slot", lfilt->workSlot);
			}
			else if (auto *rfilt = std::get_if<Plan::Rfilt>(&step))
			{
				setField("storage_slot", rfilt->storageSlot);
				setField("constant_value", rfilt->constantValue);
			}
			lua_rawseti(L, -2, stepIndex + 1);
		}
		lua_setfield(L, -2, "steps");
		return 1;
	}

	int DesignHandle::New(lua_State *L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		auto getField = [L](const char *k, auto &v) {
			lua_getfield(L, -1, k);
			if (lua_type(L, -1) != LUA_TNUMBER)
			{
				luaL_error(L, "%s is not a number", k);
			}
			v = lua_tonumber(L, -1);
			lua_pop(L, 1);
		};
		auto getArray = [L](const char *k) {
			lua_getfield(L, -1, k);
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "%s is not a table", k);
			}
			std::vector<int32_t> v(lua_objlen(L, -1));
			for (int32_t i = 0; i < int32_t(v.size()); ++i)
			{
				lua_rawgeti(L, -1, i + 1);
				if (lua_type(L, -1) != LUA_TNUMBER)
				{
					luaL_error(L, "%s[%i] is not a number", k, i + 1);
				}
				v[i] = lua_tointeger(L, -1);
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
			return v;
		};
		int32_t workSlots;
		int32_t storageSlots;
		double storageSlotOverheadPenalty;
		getField("work_slots", workSlots);
		getField("storage_slots", storageSlots);
		getField("storage_slot_overhead_penalty", storageSlotOverheadPenalty);
		auto constantValues = getArray("constants");
		auto inputStorageSlots = getArray("inputs");
		std::vector<Design::ProtoComposite> composites;
		{
			lua_getfield(L, -1, "composites");
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "composites is not a table");
			}
			composites.resize(lua_objlen(L, -1));
			for (int32_t compositeIndex = 0; compositeIndex < int32_t(composites.size()); ++compositeIndex)
			{
				lua_rawgeti(L, -1, compositeIndex + 1);
				if (lua_type(L, -1) != LUA_TTABLE)
				{
					luaL_error(L, "composites[%i] is not a table", compositeIndex + 1);
				}
				int32_t tmp;
				getField("tmp", tmp);
				if (tmp == tmpCount)
				{
					Design::ProtoSelect select;
					int32_t laneCount;
					int32_t stageCount;
					getField("lane_count", laneCount);
					getField("stage_count", stageCount);
					select.tmps = getArray("tmps");
					select.sources = getArray("sources");
					composites[compositeIndex] = select;
					auto expectedSourcesSize = laneCount * 2 + stageCount;
					if (int32_t(select.sources.size()) != expectedSourcesSize)
					{
						luaL_error(L, "composites[%i].sources is not a table with %i items", compositeIndex + 1, expectedSourcesSize);
					}
					auto expectedTmpsSize = stageCount - 1;
					if (int32_t(select.tmps.size()) != expectedTmpsSize)
					{
						luaL_error(L, "composites[%i].tmps is not a table with %i items", compositeIndex + 1, expectedTmpsSize);
					}
				}
				else
				{
					Design::ProtoBinary binary;
					binary.tmp = tmp;
					auto sources = getArray("sources");
					if (sources.size() != 2)
					{
						luaL_error(L, "composites[%i].sources is not a table with 2 items", compositeIndex + 1);
					}
					binary.rhsSource = sources[0];
					binary.lhsSource = sources[1];
					composites[compositeIndex] = binary;
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		std::vector<Design::ProtoOutputLink> outputLinks;
		{
			lua_getfield(L, -1, "outputs");
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "outputs is not a table");
			}
			outputLinks.resize(lua_objlen(L, -1));
			for (int32_t outputIndex = 0; outputIndex < int32_t(outputLinks.size()); ++outputIndex)
			{
				lua_rawgeti(L, -1, outputIndex + 1);
				if (lua_type(L, -1) != LUA_TTABLE)
				{
					luaL_error(L, "outputs[%i] is not a table", outputIndex + 1);
				}
				auto &outputLink = outputLinks[outputIndex];
				getField("source", outputLink.source);
				getField("storage_slot", outputLink.storageSlot);
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		auto *designHandle = reinterpret_cast<DesignHandle *>(lua_newuserdata(L, sizeof(DesignHandle)));
		if (!designHandle)
		{
			throw std::bad_alloc();
		}
		new(designHandle) DesignHandle();
		designHandle->design = std::make_shared<Design>(
			workSlots,
			storageSlots,
			storageSlotOverheadPenalty,
			constantValues,
			inputStorageSlots,
			composites,
			outputLinks
		);
		luaL_newmetatable(L, DesignHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int DesignHandle::Gc(lua_State *L)
	{
		auto *designHandle = reinterpret_cast<DesignHandle *>(luaL_checkudata(L, 1, DesignHandle::mtName));
		designHandle->~DesignHandle();
		return 0;
	}

	int DesignHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, DesignHandle::mtName);
		return 1;
	}

	int OptimizeOnceWrapper(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		double temperatureInitial = luaL_checknumber(L, 2);
		double temperatureFinal = luaL_checknumber(L, 3);
		double temperatureLoss = luaL_checknumber(L, 4);
		int32_t iterationCount = luaL_checkinteger(L, 5);
		uint64_t seed = luaL_checkinteger(L, 6);
		std::mt19937_64 rng(seed);
		auto ostate = OptimizeOnce(rng, *stateHandle->state, { iterationCount, temperatureInitial, temperatureFinal, temperatureLoss });
		MakeStateHandle(L, std::make_shared<State>(*ostate.state));
		lua_pushnumber(L, ostate.temperature);
		return 2;
	}

	int OptimizerHandle::New(lua_State *L)
	{
		uint64_t seed = luaL_checkinteger(L, 1);
		uint32_t threadCount = luaL_optinteger(L, 2, std::thread::hardware_concurrency());
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(lua_newuserdata(L, sizeof(OptimizerHandle)));
		if (!optimizerHandle)
		{
			throw std::bad_alloc();
		}
		new(optimizerHandle) OptimizerHandle();
		optimizerHandle->optimizer = std::make_shared<Optimizer>();
		optimizerHandle->optimizer->rng.seed(seed);
		optimizerHandle->optimizer->threadCount = threadCount;
		luaL_newmetatable(L, OptimizerHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int OptimizerHandle::Dispatch(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		if (optimizerHandle->optimizer->Dispatched())
		{
			return luaL_error(L, "optimizer is dispatched");
		}
		double temperatureFinal = luaL_checknumber(L, 2);
		double temperatureLoss = luaL_checknumber(L, 3);
		int32_t iterationCount = luaL_checkinteger(L, 4);
		optimizerHandle->optimizer->Dispatch({ iterationCount, temperatureFinal, temperatureLoss });
		return 0;
	}

	int OptimizerHandle::Gc(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		optimizerHandle->~OptimizerHandle();
		return 0;
	}

	int OptimizerHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, OptimizerHandle::mtName);
		return 1;
	}

	int OptimizerHandle::Wait(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		optimizerHandle->optimizer->Wait();
		return 0;
	}

	int OptimizerHandle::Cancel(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		optimizerHandle->optimizer->Cancel();
		return 0;
	}

	int OptimizerHandle::StateWrapper(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		if (optimizerHandle->optimizer->Dispatched())
		{
			return luaL_error(L, "optimizer is dispatched");
		}
		if (lua_gettop(L) < 2)
		{
			auto ostate = optimizerHandle->optimizer->PeekState();
			return MakeStateHandle(L, std::make_shared<State>(*ostate.state));
			lua_pushnumber(L, ostate.temperature);
			return 2;
		}
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 2, StateHandle::mtName));
		double temperatureInitial = luaL_checknumber(L, 3);
		optimizerHandle->optimizer->PokeState({ stateHandle->state, temperatureInitial });
		return 0;
	}

	int OptimizerHandle::Ready(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		lua_pushboolean(L, optimizerHandle->optimizer->Ready());
		return 1;
	}

	int OptimizerHandle::Dispatched(lua_State *L)
	{
		auto *optimizerHandle = reinterpret_cast<OptimizerHandle *>(luaL_checkudata(L, 1, OptimizerHandle::mtName));
		lua_pushboolean(L, optimizerHandle->optimizer->Dispatched());
		return 1;
	}
}

extern "C" int luaopen_spaghetti_optimize(lua_State *L)
{
	{
		static const luaL_Reg optimizerReg[] = {
			{ "wait", OptimizerHandle::Wait },
			{ "cancel", OptimizerHandle::Cancel },
			{ "state", OptimizerHandle::StateWrapper },
			{ "ready", OptimizerHandle::Ready },
			{ "dispatched", OptimizerHandle::Dispatched },
			{ "dispatch", OptimizerHandle::Dispatch },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, OptimizerHandle::mtName);
		static const luaL_Reg optimizerMt[] = {
			{ "__gc", OptimizerHandle::Gc },
			{ "__tostring", OptimizerHandle::Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, optimizerMt);
		lua_newtable(L);
		luaL_register(L, NULL, optimizerReg);
		lua_setfield(L, -2, "__index");
		lua_pop(L, 1);
	}
	{
		static const luaL_Reg stateReg[] = {
			{ "dump", StateHandle::Dump },
			{ "energy", StateHandle::EnergyWrapper },
			{ "plan", StateHandle::PlanWrapper },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, StateHandle::mtName);
		static const luaL_Reg stateMt[] = {
			{ "__gc", StateHandle::Gc },
			{ "__tostring", StateHandle::Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, stateMt);
		lua_newtable(L);
		luaL_register(L, NULL, stateReg);
		lua_setfield(L, -2, "__index");
		lua_pop(L, 1);
	}
	{
		static const luaL_Reg designReg[] = {
			{ "initial", DesignHandle::Initial },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, DesignHandle::mtName);
		static const luaL_Reg designMt[] = {
			{ "__gc", DesignHandle::Gc },
			{ "__tostring", DesignHandle::Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, designMt);
		lua_newtable(L);
		luaL_register(L, NULL, designReg);
		lua_setfield(L, -2, "__index");
		lua_pop(L, 1);
	}
	lua_newtable(L);
	{
		static const luaL_Reg optimizeReg[] = {
			{ "optimize_once", OptimizeOnceWrapper },
			{ "make_design", DesignHandle::New },
			{ "make_optimizer", OptimizerHandle::New },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, optimizeReg);
	}
	return 1;
}
