#include "DesignHandle.hpp"
#include "Common.hpp"
#include "Design.hpp"
#include "StateHandle.hpp"
#include <lua.hpp>

namespace Spaghetti::Optimize
{
	int DesignHandle::Initial(lua_State *L)
	{
		auto *designHandle = reinterpret_cast<DesignHandle *>(luaL_checkudata(L, 1, DesignHandle::mtName));
		return StateHandle::New(L, designHandle->design->Initial());
	}

	int DesignHandle::New(lua_State *L)
	{
		luaL_checktype(L, 1, LUA_TTABLE);
		auto stacks = GetField<int32_t>(L, "stacks");
		auto workSlots = GetField<int32_t>(L, "work_slots");
		auto stackMaxSize = GetField<int32_t>(L, "stack_max_size");
		auto storageSlots = GetField<int32_t>(L, "storage_slots");
		auto storageSlotOverheadPenalty = GetField<double>(L, "storage_slot_overhead_penalty");
		auto workSlotOverheadPenalty = GetField<double>(L, "work_slot_overhead_penalty");
		auto constantValues = GetArray<int32_t>(L, "constants");
		auto inputStorageSlots = GetArray<int32_t>(L, "inputs");
		auto inputInitials = GetArray<int32_t>(L, "input_initials");
		auto clobberStorageSlots = GetArray<int32_t>(L, "clobbers");
		auto voidStorageSlots = GetArray<int32_t>(L, "voids");
		std::vector<Design::ProtoComposite> composites;
		{
			lua_getfield(L, -1, "composites");
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "composites is not a table");
			}
			composites.resize(lua_objlen(L, -1));
			for (int32_t compositeIndex = 0; compositeIndex < int32_t(composites.size()); ++compositeIndex)
			{
				lua_rawgeti(L, -1, compositeIndex + 1);
				if (lua_type(L, -1) != LUA_TTABLE)
				{
					luaL_error(L, "composites[%i] is not a table", compositeIndex + 1);
				}
				auto tmp = GetField<int32_t>(L, "tmp");
				if (tmp == tmpCount)
				{
					Design::ProtoSelect select;
					auto laneCount = GetField<int32_t>(L, "lane_count");
					auto stageCount = GetField<int32_t>(L, "stage_count");
					select.tmps = GetArray<int32_t>(L, "tmps");
					select.sources = GetArray<int32_t>(L, "sources");
					composites[compositeIndex] = select;
					auto expectedSourcesSize = laneCount * 2 + stageCount;
					if (int32_t(select.sources.size()) != expectedSourcesSize)
					{
						luaL_error(L, "composites[%i].sources is not a table with %i items", compositeIndex + 1, expectedSourcesSize);
					}
					auto expectedTmpsSize = stageCount - 1;
					if (int32_t(select.tmps.size()) != expectedTmpsSize)
					{
						luaL_error(L, "composites[%i].tmps is not a table with %i items", compositeIndex + 1, expectedTmpsSize);
					}
				}
				else
				{
					Design::ProtoBinary binary;
					binary.tmp = tmp;
					auto sources = GetArray<int32_t>(L, "sources");
					if (sources.size() != 2)
					{
						luaL_error(L, "composites[%i].sources is not a table with 2 items", compositeIndex + 1);
					}
					binary.rhsSource = sources[0];
					binary.lhsSource = sources[1];
					composites[compositeIndex] = binary;
				}
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		std::vector<Design::ProtoOutputLink> outputLinks;
		{
			lua_getfield(L, -1, "outputs");
			if (lua_type(L, -1) != LUA_TTABLE)
			{
				luaL_error(L, "outputs is not a table");
			}
			outputLinks.resize(lua_objlen(L, -1));
			for (int32_t outputIndex = 0; outputIndex < int32_t(outputLinks.size()); ++outputIndex)
			{
				lua_rawgeti(L, -1, outputIndex + 1);
				if (lua_type(L, -1) != LUA_TTABLE)
				{
					luaL_error(L, "outputs[%i] is not a table", outputIndex + 1);
				}
				auto &outputLink = outputLinks[outputIndex];
				outputLink.source = GetField<int32_t>(L, "source");
				outputLink.storageSlot = GetField<int32_t>(L, "storage_slot");
				lua_pop(L, 1);
			}
			lua_pop(L, 1);
		}
		auto *designHandle = reinterpret_cast<DesignHandle *>(lua_newuserdata(L, sizeof(DesignHandle)));
		if (!designHandle)
		{
			throw std::bad_alloc();
		}
		new(designHandle) DesignHandle();
		try
		{
			designHandle->design = std::make_shared<Design>(
				stacks,
				workSlots,
				stackMaxSize,
				storageSlots,
				storageSlotOverheadPenalty,
				workSlotOverheadPenalty,
				constantValues,
				inputStorageSlots,
				inputInitials,
				clobberStorageSlots,
				voidStorageSlots,
				composites,
				outputLinks
			);
		}
		catch (const RangeCheckFailed &ex)
		{
			return luaL_error(L, "%s", ex.what());
		}
		luaL_newmetatable(L, DesignHandle::mtName);
		lua_setmetatable(L, -2);
		return 1;
	}

	int DesignHandle::Gc(lua_State *L)
	{
		auto *designHandle = reinterpret_cast<DesignHandle *>(luaL_checkudata(L, 1, DesignHandle::mtName));
		designHandle->~DesignHandle();
		return 0;
	}

	int DesignHandle::Tostring(lua_State *L)
	{
		lua_pushstring(L, DesignHandle::mtName);
		return 1;
	}

	void DesignHandle::Register(lua_State *L, int into)
	{
		lua_pushvalue(L, into);
		static const luaL_Reg designReg[] = {
			{ "initial", Initial },
			{ NULL, NULL }
		};
		luaL_newmetatable(L, mtName);
		lua_pushstring(L, mtName);
		lua_setfield(L, -2, "check_mt_name");
		static const luaL_Reg designMt[] = {
			{ "__gc"      , Gc       },
			{ "__tostring", Tostring },
			{ NULL, NULL }
		};
		luaL_register(L, NULL, designMt);
		lua_newtable(L);
		luaL_register(L, NULL, designReg);
		lua_setfield(L, -2, "__index");
		lua_setfield(L, -2, "design_mt");
		lua_pushcfunction(L, New);
		lua_setfield(L, -2, "make_design");
		lua_pop(L, 1);
	}
}
