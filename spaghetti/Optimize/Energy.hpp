#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <variant>
#include <vector>

namespace Spaghetti::Optimize
{
	class State;
	class Design;
	struct Plan;

	class Energy
	{
	public:
		std::shared_ptr<const Design> design;
		double linear;
		int32_t storageSlotCount;
		int32_t workSlotCount;
		int32_t partCount = 0;
	};

	class EnergyWithPlan : public Energy
	{
	public:
		struct StepBase
		{
			int32_t layerIndex;
		};

		struct Constant : public StepBase
		{
			static constexpr int32_t layerOrder = 0;
			int32_t storageSlot;
			int32_t value;
		};

		struct Commit : public StepBase
		{
			static constexpr int32_t layerOrder = 5;
		};

		struct Load : public StepBase
		{
			static constexpr int32_t layerOrder = 0;
			int32_t nodeIndex;
			int32_t tmp;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Cload : public StepBase
		{
			static constexpr int32_t layerOrder = 0;
			int32_t nodeIndex;
			int32_t tmp;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Mode : public StepBase
		{
			static constexpr int32_t layerOrder = 0;
			int32_t workSlot;
			int32_t tmp;
		};

		struct Store : public StepBase
		{
			static constexpr int32_t layerOrder = 1;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct Cstore : public StepBase
		{
			static constexpr int32_t layerOrder = 1;
			int32_t workSlot;
			int32_t storageSlot;
		};

		struct AllocStorage : public StepBase
		{
			static constexpr int32_t layerOrder = 4;
			int32_t sourceIndex;
			int32_t storageSlot;
			int32_t uses;
		};

		struct UseStorage : public StepBase
		{
			static constexpr int32_t layerOrder = 3;
			int32_t storageSlot;
		};

		using Step = std::variant<
			Commit,
			Load,
			Cload,
			Mode,
			Store,
			Cstore,
			AllocStorage,
			UseStorage,
			Constant
		>;

	private:
		std::vector<Step> steps;

		void MoveWorkSlot0GroupsBack();
		void SortSteps();

	public:
		struct ToPlanFailed : public std::runtime_error
		{
			using runtime_error::runtime_error;
		};
		struct StackBudgetExceeded : public ToPlanFailed
		{
			StackBudgetExceeded() : ToPlanFailed("stack count exceeded")
			{
			}
		};
		struct StorageSlotBudgetExceeded : public ToPlanFailed
		{
			StorageSlotBudgetExceeded() : ToPlanFailed("storage slot budget exceeded")
			{
			}
		};
		struct WorkSlotBudgetExceeded : public ToPlanFailed
		{
			WorkSlotBudgetExceeded() : ToPlanFailed("work slot budget exceeded")
			{
			}
		};
		std::shared_ptr<Plan> ToPlan() const;

		const std::vector<Step> &GetSteps() const
		{
			return steps;
		}

		friend class State;
		friend std::ostream &operator <<(std::ostream &stream, const State &state);
	};
}
