#pragma once
#include <cstdint>
#include <optional>
#include <vector>

namespace Spaghetti::Occsat
{
	struct Solution
	{
		std::optional<std::vector<int32_t>> satisfiable;
	};
}
