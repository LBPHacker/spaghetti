#pragma once
#include <cstdint>
#include <vector>

namespace Spaghetti::Occsat
{
	struct Problem
	{
		int32_t varCount = 0;
		std::vector<std::vector<int32_t>> clauses;
	};
}
