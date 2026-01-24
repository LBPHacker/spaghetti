#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Occsat
{
	class Solver;

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

		static void Register(lua_State *L, int into);
	};
}
