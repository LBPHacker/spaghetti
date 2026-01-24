#include "DesignHandle.hpp"
#include "OptimizerHandle.hpp"
#include "PlanValue.hpp"
#include "ScheduleHandle.hpp"
#include "StateHandle.hpp"
#include <lua.hpp>
#include <thread>

namespace
{
	int HardwareConcurrency(lua_State *L)
	{
		lua_pushinteger(L, std::thread::hardware_concurrency());
		return 1;
	}

	int Sleep(lua_State *L)
	{
		std::this_thread::sleep_for(std::chrono::nanoseconds(int64_t(luaL_checknumber(L, 1) * 1e9)));
		return 0;
	}
}

extern "C" int luaopen_spaghetti_optimize(lua_State *L)
{
	using namespace Spaghetti::Optimize;
	lua_newtable(L);
	OptimizerHandle::Register(L, -1);
	StateHandle::Register(L, -1);
	DesignHandle::Register(L, -1);
	PlanValue::Register(L, -1);
	ScheduleHandle::Register(L, -1);
	lua_pushcfunction(L, HardwareConcurrency);
	lua_setfield(L, -2, "hardware_concurrency");
	lua_pushcfunction(L, Sleep);
	lua_setfield(L, -2, "sleep");
	return 1;
}
