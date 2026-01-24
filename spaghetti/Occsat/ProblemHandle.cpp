#include "ProblemHandle.hpp"
#include "Problem.hpp"
#include <lua.hpp>

namespace Spaghetti::Occsat
{
	int ProblemHandle::New(lua_State *L)
	{
		auto varCount = int32_t(luaL_checknumber(L, 1));
		luaL_checktype(L, 2, LUA_TTABLE);
		auto *problemHandle = reinterpret_cast<ProblemHandle *>(lua_newuserdata(L, sizeof(ProblemHandle)));
		if (!problemHandle)
		{
			throw std::bad_alloc();
		}
		new(problemHandle) ProblemHandle();
		auto newProblem = std::make_shared<Problem>();
		auto clauseCount = int32_t(lua_objlen(L, 2));
		newProblem->clauses.resize(clauseCount);
		for (int32_t i = 0; i < clauseCount; ++i)
		{
			lua_rawgeti(L, 2, i + 1);
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "clauses[%i] is not a table", i + 1);
			}
			auto termCount = int32_t(lua_objlen(L, -1));
			newProblem->clauses[i].resize(termCount);
			for (int32_t j = 0; j < termCount; ++j)
			{
				lua_rawgeti(L, -1, j + 1);
				if (lua_type(L, -1) != LUA_TNUMBER)
				{
					luaL_error(L, "clauses[%i][%i] is not a number", i + 1, j + 1);
				}
				newProblem->clauses[i][j] = lua_tonumber(L, -1);
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		newProblem->varCount = varCount;
		problemHandle->problem = newProblem;
		luaL_newmetatable(L, ProblemHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int ProblemHandle::Gc(lua_State *L)
	{
		auto *problemHandle = reinterpret_cast<ProblemHandle *>(luaL_checkudata(L, 1, ProblemHandle::mtName));
		problemHandle->~ProblemHandle();
		return 0;
	}

	int ProblemHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, ProblemHandle::mtName);
		return 1;
	}

	void ProblemHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg problemReg[] = {
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg problemMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, problemMt);
		lua_newtable(L);
		luaL_register(L, NULL, problemReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "problem_mt");
		lua_pushcfunction(L, New);
		lua_setfield(L, -2, "make_problem");
		lua_pop(L, 1);
	}
}
