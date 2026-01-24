#pragma once
#include <memory>

typedef struct lua_State lua_State;

namespace Spaghetti::Optimize
{
	struct Schedule;

	struct ScheduleHandle
	{
		static constexpr auto mtName = "spaghetti.optimize.schedule";
		std::shared_ptr<const Schedule> schedule;

		static int NewInternal(lua_State *L, std::shared_ptr<const Schedule> schedule);
		static int New(lua_State *L);
		static int Gc(lua_State *L);
		static int Tostring(lua_State *L);
		static int Duration(lua_State *L);

		static void Register(lua_State *L, int into);
	};
}
