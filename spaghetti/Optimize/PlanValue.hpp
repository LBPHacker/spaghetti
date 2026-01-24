#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Optimize
{
	struct PlanValue
	{
		static constexpr auto mtName = "spaghetti.optimize.plan";

		static int Tostring(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
