#include "SolverHandle.hpp"
#include "ProblemHandle.hpp"
#include <lua.hpp>

extern "C" int luaopen_spaghetti_occsat(lua_State *L)
{
	using namespace Spaghetti::Occsat;
	lua_newtable(L);
	ProblemHandle::Register(L, -1);
	SolverHandle::Register(L, -1);
	return 1;
}
