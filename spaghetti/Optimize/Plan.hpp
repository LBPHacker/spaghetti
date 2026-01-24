#pragma once
#include <cstdint>
#include <variant>
#include <vector>

namespace Spaghetti::Optimize
{
	struct Plan
	{
		struct StepBase
		{
			int32_t stackIndex;
		};

		struct Lcap : public StepBase
		{
			static constexpr int32_t cost = 2;
			int32_t life3Index;
		};

		struct Lfilt : public StepBase
		{
			static constexpr int32_t cost = 1;
			int32_t workSlot;
		};

		struct Rfilt : public StepBase
		{
			static constexpr int32_t cost = 1;
			int32_t storageSlot;
			int32_t constantValue = -1;
		};

		struct Top : public StepBase
		{
			static constexpr int32_t cost = 6;
		};

		struct Bottom : public StepBase
		{
			static constexpr int32_t cost = 6;
		};

		struct Load : public StepBase
		{
			static constexpr int32_t cost = 2;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Cload : public StepBase
		{
			static constexpr int32_t cost = 1;
			int32_t workSlot;
		};

		struct Mode : public StepBase
		{
			static constexpr int32_t cost = 2;
			int32_t tmp;
		};

		struct Store : public StepBase
		{
			static constexpr int32_t cost = 2;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Cstore : public StepBase
		{
			static constexpr int32_t cost = 1;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Aray : public StepBase
		{
			static constexpr int32_t cost = 5;
		};

		struct East : public StepBase
		{
			static constexpr int32_t cost = 6;
		};

		struct West : public StepBase
		{
			static constexpr int32_t cost = 6;
		};

		struct Clear : public StepBase
		{
			static constexpr int32_t cost = 1;
		};

		using Step = std::variant<
			Load,
			Cload,
			Mode,
			Store,
			Cstore,
			Aray,
			East,
			West,
			Clear,
			Top,
			Bottom,
			Lcap,
			Lfilt,
			Rfilt
		>;
		std::vector<Step> steps;
		int32_t cost = 0;
		int32_t stacksUsed;

		static constexpr auto commitCost = Aray::cost + East::cost + West::cost + Clear::cost;
	};
}
