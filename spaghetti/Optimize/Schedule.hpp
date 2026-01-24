#pragma once
#include <algorithm>
#include <cstdint>
#include <vector>

namespace Spaghetti::Optimize
{
	struct Schedule
	{
		struct Step
		{
			int64_t beginsAt;
			double temperature;
		};
		std::vector<Step> steps;
		int64_t endsAt = 0;

		int64_t NormalizeProgress(int64_t progress) const
		{
			return std::max(std::min(progress, endsAt), int64_t(0));
		}
	};
}
