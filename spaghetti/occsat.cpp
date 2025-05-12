#include "occsat.hpp"
#include <cassert>
#include <cstring>
#include <lua.hpp>

namespace Spaghetti
{
	void Solver::Dispatch()
	{
		assert(!dispatched);
		ready = false;
		solution.reset();
		cancelRequest = false;
		dispatched = true;
		thr = std::thread([this]() {
			Thread();
			ready = true;
		});
	}

	void Solver::Wait()
	{
		if (dispatched)
		{
			thr.join();
			dispatched = false;
		}
	}

	void Solver::Cancel()
	{
		{
			std::lock_guard lk(cancelRequestMx);
			cancelRequest = true;
		}
		cancelRequestCv.notify_all();
		Wait();
	}

	void Solver::SetProblem(std::shared_ptr<const Problem> newProblem)
	{
		problem = newProblem;
	}

	Solver::~Solver()
	{
		Cancel();
	}

	namespace
	{
		struct ProblemHandle
		{
			static constexpr auto mtName = "spaghetti.occsat.problem";
			std::shared_ptr<const Problem> problem;

			static int New(lua_State *L);
			static int Gc(lua_State *L);
			static int Tostring(lua_State *L);
		};

		struct SolverHandle
		{
			static constexpr auto mtName = "spaghetti.occsat.solver";
			std::shared_ptr<Solver> solver;

			static int New(lua_State *L);
			static int Gc(lua_State *L);
			static int Tostring(lua_State *L);
			static int Wait(lua_State *L);
			static int Cancel(lua_State *L);
			static int Ready(lua_State *L);
			static int Dispatched(lua_State *L);
			static int Dispatch(lua_State *L);
			static int Problem(lua_State *L);
			static int Solution(lua_State *L);
		};

		int ProblemHandle::New(lua_State *L)
		{
			auto varCount = int32_t(luaL_checknumber(L, 1));
			luaL_checktype(L, 2, LUA_TTABLE);
			auto *problemHandle = reinterpret_cast<ProblemHandle *>(lua_newuserdata(L, sizeof(ProblemHandle)));
			if (!problemHandle)
			{
				throw std::bad_alloc();
			}
			new(problemHandle) ProblemHandle();
			auto newProblem = std::make_shared<Problem>();
			auto clauseCount = int32_t(lua_objlen(L, 2));
			newProblem->clauses.resize(clauseCount);
			for (int32_t i = 0; i < clauseCount; ++i)
			{
				lua_rawgeti(L, 2, i + 1);
				if (lua_type(L, -1) != LUA_TTABLE)
				{
					luaL_error(L, "clauses[%i] is not a table", i + 1);
				}
				auto termCount = int32_t(lua_objlen(L, -1));
				newProblem->clauses[i].resize(termCount);
				for (int32_t j = 0; j < termCount; ++j)
				{
					lua_rawgeti(L, -1, j + 1);
					if (lua_type(L, -1) != LUA_TNUMBER)
					{
						luaL_error(L, "clauses[%i][%i] is not a number", i + 1, j + 1);
					}
					newProblem->clauses[i][j] = lua_tonumber(L, -1);
					lua_pop(L, 1);
				}
				lua_pop(L, 1);
			}
			newProblem->varCount = varCount;
			problemHandle->problem = newProblem;
			luaL_newmetatable(L, ProblemHandle::mtName);
			lua_setmetatable(L, -2);
			return 1;
		}

		int ProblemHandle::Gc(lua_State *L)
		{
			auto *problemHandle = reinterpret_cast<ProblemHandle *>(luaL_checkudata(L, 1, ProblemHandle::mtName));
			problemHandle->~ProblemHandle();
			return 0;
		}

		int ProblemHandle::Tostring(lua_State *L)
		{
			lua_pushstring(L, ProblemHandle::mtName);
			return 1;
		}

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
#define OCSSAT_MAKE(name) if (!solverHandle->solver && (!requested || !std::strcmp(requested, #name))) solverHandle->solver = Solver::Make_ ## name();
			OCSSAT_SOLVERS(OCSSAT_MAKE)
#undef OCSSAT_MAKE
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
	}
}

extern "C" int luaopen_spaghetti_occsat(lua_State *L)
{
	using namespace Spaghetti;
	lua_newtable(L);
	{
		static const luaL_Reg problemReg[] = {
			{ NULL, NULL }
		};
		luaL_newmetatable(L, ProblemHandle::mtName);
		lua_pushstring(L, ProblemHandle::mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg problemMt[] = {
			{ "__gc"      , ProblemHandle::Gc       },
			{ "__tostring", ProblemHandle::Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, problemMt);
		lua_newtable(L);
		luaL_register(L, NULL, problemReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "problem_mt");
	}
	{
		static const luaL_Reg solverReg[] = {
			{ "wait"      , SolverHandle::Wait       },
			{ "cancel"    , SolverHandle::Cancel     },
			{ "problem"   , SolverHandle::Problem    },
			{ "solution"  , SolverHandle::Solution   },
			{ "ready"     , SolverHandle::Ready      },
			{ "dispatched", SolverHandle::Dispatched },
			{ "dispatch"  , SolverHandle::Dispatch   },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, SolverHandle::mtName);
		lua_pushstring(L, SolverHandle::mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg solverMt[] = {
			{ "__gc"      , SolverHandle::Gc       },
			{ "__tostring", SolverHandle::Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, solverMt);
		lua_newtable(L);
		luaL_register(L, NULL, solverReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "solver_mt");
	}
	{
		static const luaL_Reg occsatReg[] = {
			{ "make_problem", ProblemHandle::New },
			{ "make_solver" , SolverHandle::New  },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, occsatReg);
	}
	return 1;
}
