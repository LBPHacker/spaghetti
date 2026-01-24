#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Optimize
{
	class Optimizer;

	struct OptimizerHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.optimizer";
		std::shared_ptr<Optimizer> optimizer;

		static int Once(lua_State *L);
		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Wait(lua_State *L);
		static int Cancel(lua_State *L);
		static int StateWrapper(lua_State *L);
		static int Ready(lua_State *L);
		static int Dispatched(lua_State *L);
		static int Dispatch(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
