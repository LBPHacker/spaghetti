#pragma once
#include <array>
#include <cstdint>
#include <cstddef>

namespace Spaghetti::Optimize
{
	struct Link
	{
		enum Direction
		{
			directionUp,
			directionDown,
			directionMax,
		};
		enum LinkType
		{
			toBinary,
			toSelectNonzero,
			toSelectZero,
			toOutput,
		} type;
		struct Neighbour
		{
			int32_t nodeIndex = -1;
			int32_t linkIndicesIndex = -1;
		};
		std::array<Neighbour, directionMax> neighbours;
		int32_t upstreamOutputIndex = -1;
	};
}
