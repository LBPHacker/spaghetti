#include "OptimizerHandle.hpp"
#include "Optimizer.hpp"
#include "StateHandle.hpp"
#include "State.hpp"
#include "ScheduleHandle.hpp"
#include "Schedule.hpp"
#include <lua.hpp>

namespace Spaghetti::Optimize
{
	namespace
	{
		uint64_t GetSeed(lua_State *L, int stackIndex)
		{
			uint64_t seed0 = luaL_checkinteger(L, stackIndex);
			uint64_t seed1 = luaL_checkinteger(L, stackIndex + 1);
			return (seed1 << 32) | seed0;
		}
	}

	int OptimizerHandle::Once(lua_State *L)
	{
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 1, StateHandle::mtName));
		auto *scheduleHandle = reinterpret_cast<ScheduleHandle *>(luaL_checkudata(L, 2, ScheduleHandle::mtName));
		int64_t progress = scheduleHandle->schedule->NormalizeProgress(luaL_checkinteger(L, 3));
		int32_t iterationCount = luaL_checkinteger(L, 4);
		auto seed = GetSeed(L, 5);
		std::mt19937_64 rng(seed);
		auto ostate = Optimizer::OptimizeOnce(rng, { stateHandle->state, scheduleHandle->schedule, progress }, iterationCount);
		StateHandle::New(L, std::make_shared<State>(*ostate.state));
		lua_pushnumber(L, ostate.progress);
		return 2;
	}

	int OptimizerHandle::New(lua_State *L)
	{
		auto seed = GetSeed(L, 1);
		uint32_t threadCount = luaL_optinteger(L, 3, 1);
		if (!(threadCount > 0))
		{
			return luaL_error(L, "thread_count must be positive");
		}
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
		Optimizer::DispatchParameters dp;
		dp.iterationCount = luaL_checkinteger(L, 2);
		dp.roundsPerExchange = luaL_optinteger(L, 3, 0);
		optimizerHandle->optimizer->Dispatch(dp);
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
		if (lua_gettop(L) < 2)
		{
			auto ostate = optimizerHandle->optimizer->PeekState();
			StateHandle::New(L, std::make_shared<State>(*ostate.state));
			ScheduleHandle::NewInternal(L, ostate.schedule);
			lua_pushinteger(L, ostate.progress);
			lua_pushnumber(L, ostate.Temperature());
			return 4;
		}
		// TODO: stupid design, fix
		if (optimizerHandle->optimizer->Dispatched() && optimizerHandle->optimizer->Ready())
		{
			optimizerHandle->optimizer->Wait();
		}
		if (optimizerHandle->optimizer->Dispatched())
		{
			return luaL_error(L, "optimizer is dispatched");
		}
		auto *stateHandle = reinterpret_cast<StateHandle *>(luaL_checkudata(L, 2, StateHandle::mtName));
		auto *scheduleHandle = reinterpret_cast<ScheduleHandle *>(luaL_checkudata(L, 3, ScheduleHandle::mtName));
		int64_t progress = scheduleHandle->schedule->NormalizeProgress(luaL_checkinteger(L, 4));
		optimizerHandle->optimizer->PokeState({ stateHandle->state, scheduleHandle->schedule, progress });
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

	void OptimizerHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg optimizerReg[] = {
			{ "wait"      , Wait         },
			{ "cancel"    , Cancel       },
			{ "state"     , StateWrapper },
			{ "ready"     , Ready        },
			{ "dispatched", Dispatched   },
			{ "dispatch"  , Dispatch     },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg optimizerMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, optimizerMt);
		lua_newtable(L);
		luaL_register(L, NULL, optimizerReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "optimizer_mt");
		lua_pushcfunction(L, New);
		lua_setfield(L, -2, "make_optimizer");
		lua_pushcfunction(L, Once);
		lua_setfield(L, -2, "optimize_once");
		lua_pop(L, 1);
	}
}
