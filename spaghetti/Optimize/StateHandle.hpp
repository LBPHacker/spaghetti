#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Optimize
{
	class State;

	struct StateHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.state";
		std::shared_ptr<State> state;

		static int New(lua_State *L, std::shared_ptr<State> state);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Dump(lua_State *L);
		static int EnergyWrapper(lua_State *L);
		static int PlanWrapper(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
