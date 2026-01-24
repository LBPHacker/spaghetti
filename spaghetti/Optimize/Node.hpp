#pragma once
#include "Link.hpp"
#include <vector>

namespace Spaghetti::Optimize
{
	struct Node
	{
		enum
		{
			constant,
			input,
			binary,
			select,
			output,
		} type;
		std::array<std::vector<int32_t>, Link::directionMax> linkIndices;
		std::vector<int32_t> tmps;
		int32_t workSlotsNeeded = -1;
		std::vector<int32_t> sources;
	};
}
