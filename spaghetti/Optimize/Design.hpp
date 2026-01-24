#pragma once
#include "Node.hpp"
#include <memory>
#include <optional>
#include <variant>

namespace Spaghetti::Optimize
{
	class State;

	class Design : public std::enable_shared_from_this<Design>
	{
		int32_t stacks;
		int32_t workSlots;
		int32_t stackMaxCost;
		int32_t storageSlots;
		int32_t constantCount;
		int32_t inputCount;
		int32_t compositeCount;
		int32_t outputCount;
		std::vector<Node> nodes;
		std::vector<Link> links;
		std::vector<int32_t> constantValues;
		std::vector<int32_t> inputStorageSlots;
		std::vector<int32_t> inputInitials;
		std::vector<int32_t> clobberStorageSlots;
		std::vector<int32_t> voidStorageSlots;
		struct StorageSlotOutputLink
		{
			int32_t sourceIndex;
			int32_t storageSlot;
		};
		struct WorkSlotOutputLink
		{
			int32_t sourceIndex;
			int32_t workSlot;
		};
		using OutputLink = std::variant<
			StorageSlotOutputLink,
			WorkSlotOutputLink
		>;
		std::vector<OutputLink> outputLinks;
		struct Source
		{
			int32_t nodeIndex;
			int32_t outputIndex;
			int32_t uses = 0;
		};
		std::vector<Source> sources;

		double storageSlotOverheadPenalty;
		double workSlotOverheadPenalty;

		struct CheckResult
		{
			int32_t workSlots;
		};
		using NodeIndexIterator = std::vector<int32_t>::const_iterator;
		std::optional<CheckResult> CheckLayer(NodeIndexIterator nodeIndicesBegin, NodeIndexIterator nodeIndicesEnd) const;

	public:
		Design() = default;

		struct ProtoOutputLink
		{
			int32_t source;
			int32_t storageSlot;
		};
		struct ProtoBinary
		{
			int32_t tmp;
			int32_t lhsSource, rhsSource;
		};
		struct ProtoSelect
		{
			std::vector<int32_t> tmps;
			std::vector<int32_t> sources;
		};
		using ProtoComposite = std::variant<
			ProtoBinary,
			ProtoSelect
		>;
		Design(
			int32_t newStacks,
			int32_t newWorkSlots,
			int32_t newStackMaxSize,
			int32_t newStorageSlots,
			double newStorageSlotOverheadPenalty,
			double newWorkSlotOverheadPenalty,
			std::vector<int32_t> newConstantValues,
			std::vector<int32_t> newInputStorageSlots,
			std::vector<int32_t> newInputInitials,
			std::vector<int32_t> newClobberStorageSlots,
			std::vector<int32_t> newVoidStorageSlots,
			std::vector<ProtoComposite> newComposites,
			std::vector<ProtoOutputLink> newOutputLinks
		);

		std::shared_ptr<State> Initial() const;

		int32_t StorageSlots() const
		{
			return storageSlots;
		}

		int32_t WorkSlots() const
		{
			return workSlots;
		}

		int32_t StackMaxCost() const
		{
			return stackMaxCost;
		}

		int32_t Stacks() const
		{
			return stacks;
		}

		const std::vector<Node> &Nodes() const
		{
			return nodes;
		}

		const std::vector<int32_t> &VoidStorageSlots() const
		{
			return voidStorageSlots;
		}

		// TODO: get rid of this nonsense everywhere
		friend class State;
		friend class Energy;
		friend class EnergyWithPlan;
		friend std::ostream &operator <<(std::ostream &stream, const State &state);
	};
}
