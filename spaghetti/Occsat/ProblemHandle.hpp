#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Occsat
{
	struct Problem;

	struct ProblemHandle
	{
		static constexpr auto mtName = "spaghetti.occsat.problem";
		std::shared_ptr<const Problem> problem;

		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
