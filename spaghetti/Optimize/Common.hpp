#pragma once
#include <array>
#include <cstdint>
#include <lua.hpp>
#include <stdexcept>
#include <vector>

namespace Spaghetti::Optimize
{
	constexpr int32_t tmpCount       = 12;
	constexpr int32_t lsnsLife3Value = 0x10000003;

	struct RangeCheckFailed : public std::invalid_argument
	{
		using invalid_argument::invalid_argument;
	};
	inline void CheckRange(int32_t v, int32_t l, int32_t h, int errAt)
	{
		if (v < l || v >= h)
		{
			throw RangeCheckFailed("failed CheckRange at line " + std::to_string(errAt));
			exit(1);
		}
	}
#define CheckRange(v, l, h) CheckRange((v), (l), (h), __LINE__)

	constexpr std::array<int32_t, tmpCount> tmpCommutativity = {{ 0, 1, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0 }};

	template<class Item>
	auto GetField(lua_State *L, const char *k) -> Item
	{
		lua_getfield(L, -1, k);
		if (lua_type(L, -1) != LUA_TNUMBER)
		{
			luaL_error(L, "%s is not a number", k);
		}
		Item v;
		if constexpr (std::is_floating_point_v<Item>)
		{
			v = lua_tonumber(L, -1);
		}
		else
		{
			v = lua_tointeger(L, -1);
		}
		lua_pop(L, 1);
		return v;
	}

	template<class Item>
	auto GetArray(lua_State *L, const char *k) -> std::vector<Item>
	{
		lua_getfield(L, -1, k);
		if (lua_type(L, -1) != LUA_TTABLE)
		{
			luaL_error(L, "%s is not a table", k);
		}
		std::vector<Item> v(lua_objlen(L, -1));
		for (int32_t i = 0; i < int32_t(v.size()); ++i)
		{
			lua_rawgeti(L, -1, i + 1);
			if (lua_type(L, -1) != LUA_TNUMBER)
			{
				luaL_error(L, "%s[%i] is not a number", k, i + 1);
			}
			if constexpr (std::is_floating_point_v<Item>)
			{
				v[i] = lua_tonumber(L, -1);
			}
			else
			{
				v[i] = lua_tointeger(L, -1);
			}
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		return v;
	}
}
