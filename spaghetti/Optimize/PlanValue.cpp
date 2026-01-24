#include "PlanValue.hpp"
#include <lua.hpp>

namespace Spaghetti::Optimize
{
	int PlanValue::Tostring(lua_State *L)
	{
		lua_pushstring(L, PlanValue::mtName);
		return 1;
	}

	void PlanValue::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg planMt[] = {
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, planMt);
		lua_setfield(L, -2, "plan_mt");
		lua_pop(L, 1);
	}
}
