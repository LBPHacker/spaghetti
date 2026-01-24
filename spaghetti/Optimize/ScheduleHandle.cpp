#include "ScheduleHandle.hpp"
#include "Common.hpp"
#include "Schedule.hpp"
#include <lua.hpp>

namespace Spaghetti::Optimize
{
	int ScheduleHandle::NewInternal(lua_State *L, std::shared_ptr<const Schedule> schedule)
	{
		auto *scheduleHandle = reinterpret_cast<ScheduleHandle *>(lua_newuserdata(L, sizeof(ScheduleHandle)));
		if (!scheduleHandle)
		{
			throw std::bad_alloc();
		}
		new(scheduleHandle) ScheduleHandle();
		scheduleHandle->schedule = schedule;
		luaL_newmetatable(L, ScheduleHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int ScheduleHandle::New(lua_State *L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		auto durations = GetArray<int64_t>(L, "durations");
		auto temperatures = GetArray<double>(L, "temperatures");
		if (!durations.size())
		{
			return luaL_error(L, "empty schedule");
		}
		if (durations.size() + 1 != temperatures.size())
		{
			return luaL_error(L, "parameter size mismatch");
		}
		auto schedule = std::make_shared<Schedule>();
		schedule->steps.resize(temperatures.size());
		int64_t endsAt = 0;
		for (int32_t itemIndex = 0; itemIndex < int32_t(temperatures.size()); ++itemIndex)
		{
			auto last = itemIndex == int32_t(temperatures.size()) - 1;
			if (!last && !(durations[itemIndex] > 0))
			{
				return luaL_error(L, "schedule.durations[%i] must be positive", itemIndex + 1);
			}
			if (!(temperatures[itemIndex] > 0))
			{
				return luaL_error(L, "schedule.temperatures[%i] must be positive", itemIndex + 1);
			}
			schedule->steps[itemIndex].temperature = temperatures[itemIndex];
			schedule->steps[itemIndex].beginsAt = endsAt;
			if (!last)
			{
				endsAt += durations[itemIndex];
			}
		}
		schedule->endsAt = schedule->steps.back().beginsAt;
		ScheduleHandle::NewInternal(L, schedule);
		return 1;
	}

	int ScheduleHandle::Gc(lua_State *L)
	{
		auto *scheduleHandle = reinterpret_cast<ScheduleHandle *>(luaL_checkudata(L, 1, ScheduleHandle::mtName));
		scheduleHandle->~ScheduleHandle();
		return 0;
	}

	int ScheduleHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, ScheduleHandle::mtName);
		return 1;
	}

	int ScheduleHandle::Duration(lua_State *L)
	{
		auto *scheduleHandle = reinterpret_cast<ScheduleHandle *>(luaL_checkudata(L, 1, ScheduleHandle::mtName));
		lua_pushinteger(L, scheduleHandle->schedule->endsAt);
		return 1;
	}

	void ScheduleHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg scheduleReg[] = {
			{ "duration", Duration },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg scheduleMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, scheduleMt);
		lua_newtable(L);
		luaL_register(L, NULL, scheduleReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "schedule_mt");
		lua_pushcfunction(L, New);
		lua_setfield(L, -2, "make_schedule");
		lua_pop(L, 1);
	}
}
