#include "SolverHandle.hpp"
#include "ProblemHandle.hpp"
#include "Solver.hpp"
#include <lua.hpp>
#include <cstring>

namespace Spaghetti::Occsat
{
	int SolverHandle::New(lua_State *L)
	{
		[[maybe_unused]] const char *requested = nullptr;
		if (!lua_isnoneornil(L, 1))
		{
			requested = luaL_checkstring(L, 1);
		}
		auto *solverHandle = reinterpret_cast<SolverHandle *>(lua_newuserdata(L, sizeof(SolverHandle)));
		if (!solverHandle)
		{
			throw std::bad_alloc();
		}
		new(solverHandle) SolverHandle();
#define OccsatMake(name) if (!solverHandle->solver && (!requested || !std::strcmp(requested, #name))) solverHandle->solver = Solver::Make ## name();
		OccsatSolvers(OccsatMake)
#undef OccsatMake
		if (!solverHandle->solver)
		{
			luaL_error(L, "requested solver not available");
		}
		solverHandle->solver->SetThreadCount(uint32_t(luaL_optinteger(L, 2, std::thread::hardware_concurrency())));
		luaL_newmetatable(L, SolverHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int SolverHandle::Gc(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		solverHandle->~SolverHandle();
		return 0;
	}

	int SolverHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, SolverHandle::mtName);
		return 1;
	}

	int SolverHandle::Dispatch(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		if (solverHandle->solver->Dispatched())
		{
			return luaL_error(L, "optimizer is dispatched");
		}
		solverHandle->solver->Dispatch();
		return 0;
	}

	int SolverHandle::Ready(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		lua_pushboolean(L, solverHandle->solver->Ready());
		return 1;
	}

	int SolverHandle::Dispatched(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		lua_pushboolean(L, solverHandle->solver->Dispatched());
		return 1;
	}

	int SolverHandle::Wait(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		solverHandle->solver->Wait();
		return 0;
	}

	int SolverHandle::Cancel(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		solverHandle->solver->Cancel();
		return 0;
	}

	int SolverHandle::Problem(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		auto *problemHandle = reinterpret_cast<ProblemHandle *>(luaL_checkudata(L, 2, ProblemHandle::mtName));
		solverHandle->solver->SetProblem(problemHandle->problem);
		return 0;
	}

	int SolverHandle::Solution(lua_State *L)
	{
		auto *solverHandle = reinterpret_cast<SolverHandle *>(luaL_checkudata(L, 1, SolverHandle::mtName));
		auto solution = solverHandle->solver->GetSolution();
		if (!solution)
		{
			lua_pushliteral(L, "cancelled");
			return 1;
		}
		if (!solution->satisfiable)
		{
			lua_pushliteral(L, "unsatisfiable");
			return 1;
		}
		lua_pushliteral(L, "satisfiable");
		lua_newtable(L);
		int32_t index = 0;
		for (auto value : *solution->satisfiable)
		{
			index += 1;
			lua_pushinteger(L, value);
			lua_rawseti(L, -2, index);
		}
		return 2;
	}

	void SolverHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg solverReg[] = {
			{ "wait"      , Wait       },
			{ "cancel"    , Cancel     },
			{ "problem"   , Problem    },
			{ "solution"  , Solution   },
			{ "ready"     , Ready      },
			{ "dispatched", Dispatched },
			{ "dispatch"  , Dispatch   },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg solverMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, solverMt);
		lua_newtable(L);
		luaL_register(L, NULL, solverReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "solver_mt");
		lua_pushcfunction(L, New);
		lua_setfield(L, -2, "make_solver");
		lua_pop(L, 1);
	}
}
