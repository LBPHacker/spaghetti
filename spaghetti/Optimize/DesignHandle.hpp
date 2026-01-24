#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Optimize
{
	class Design;

	struct DesignHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.design";
		std::shared_ptr<Design> design;

		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Initial(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
