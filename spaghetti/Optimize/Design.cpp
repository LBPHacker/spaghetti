#include "Design.hpp"
#include "Common.hpp"
#include "State.hpp"
#include <algorithm>

namespace Spaghetti::Optimize
{
	std::shared_ptr<State> Design::Initial() const
	{
		auto state = std::make_shared<State>();
		state->design = shared_from_this();
		state->iteration = 0;
		for (int32_t nodeIndex = 0; nodeIndex < int32_t(nodes.size()); ++nodeIndex)
		{
			state->nodeIndices.push_back(nodeIndex);
		}
		state->layers.push_back(0);
		for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
		{
			state->layers.push_back(constantCount + inputCount + compositeIndex);
		}
		state->layers.push_back(constantCount + inputCount + compositeCount);
		return state;
	}

	std::optional<Design::CheckResult> Design::CheckLayer(NodeIndexIterator nodeIndicesBegin, NodeIndexIterator nodeIndicesEnd) const
	{
		auto size = nodeIndicesEnd - nodeIndicesBegin;
		// we assume that node order between layers is correct
		// but we detect node order violations within the layer
		for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(size) - 1; ++nodeIndicesIndex)
		{
			auto &node = nodes[nodeIndicesBegin[nodeIndicesIndex]];
			if (node.type == Node::select)
			{
				// select somewhere other than at the end
				return std::nullopt;
			}
		}
		CheckResult checkResult;
		checkResult.workSlots = 0;
		auto nodeIndexInLayer = [nodeIndicesBegin, nodeIndicesEnd](int32_t nodeIndex) -> std::optional<int32_t> {
			auto it = std::find(nodeIndicesBegin, nodeIndicesEnd, nodeIndex);
			if (it == nodeIndicesEnd)
			{
				return std::nullopt;
			}
			return int32_t(it - nodeIndicesBegin);
		};
		for (int32_t nodeIndicesIndex = 0; nodeIndicesIndex < int32_t(size); ++nodeIndicesIndex)
		{
			auto &node = nodes[nodeIndicesBegin[nodeIndicesIndex]];
			checkResult.workSlots += node.workSlotsNeeded;
			int32_t sameLayerBinaryLinkCount = 0;
			for (auto linkIndex : node.linkIndices[Link::directionDown])
			{
				auto &link = links[linkIndex];
				auto linkedNodeIndex = link.neighbours[Link::directionDown].nodeIndex;
				auto &linkedNode = nodes[linkedNodeIndex];
				auto linkedNodeIndexInLayer = nodeIndexInLayer(linkedNodeIndex);
				if (linkedNodeIndexInLayer)
				{
					if (link.type == Link::toBinary)
					{
						if (*linkedNodeIndexInLayer != nodeIndicesIndex + 1)
						{
							// binary same-layer link with non-adjacent node
							return std::nullopt;
						}
						int32_t lhsIndex = 1;
						if (linkedNode.type == Node::select)
						{
							auto laneCount = int32_t(linkedNode.sources.size());
							lhsIndex += laneCount * 2;
						}
						if (link.neighbours[Link::directionDown].linkIndicesIndex == lhsIndex && !tmpCommutativity[linkedNode.tmps[0]])
						{
							// binary same-layer link to lhs of non-commutative node
							return std::nullopt;
						}
						if (link.neighbours[Link::directionDown].linkIndicesIndex > lhsIndex)
						{
							// binary same-layer link to parameter of higher index than that of rhs or lhs
							return std::nullopt;
						}
						sameLayerBinaryLinkCount += 1;
						if (sameLayerBinaryLinkCount > 1)
						{
							// multiple binary same-layer links
							return std::nullopt;
						}
					}
					if (link.type == Link::toSelectNonzero)
					{
						// nonzero same-layer link
						return std::nullopt;
					}
					if (link.type == Link::toBinary || link.type == Link::toSelectZero)
					{
						// this saves a load
						checkResult.workSlots -= 1;
					}
				}
			}
		}
		return checkResult;
	}

	Design::Design(
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
	)
	{
		constexpr int32_t bigNumber = 10000;
		constantCount = newConstantValues.size();
		stacks = newStacks;
		workSlots = newWorkSlots;
		stackMaxCost = newStackMaxSize;
		storageSlots = newStorageSlots;
		storageSlotOverheadPenalty = newStorageSlotOverheadPenalty;
		workSlotOverheadPenalty = newWorkSlotOverheadPenalty;
		inputCount = newInputStorageSlots.size();
		CheckRange(newInputInitials.size(), inputCount, inputCount + 1);
		compositeCount = newComposites.size();
		outputCount = newOutputLinks.size();
		CheckRange(stacks, 1, bigNumber);
		CheckRange(workSlots, 2, bigNumber);
		CheckRange(storageSlots, 1, bigNumber);
		CheckRange(constantCount, 0, bigNumber);
		CheckRange(inputCount, 1, bigNumber);
		CheckRange(compositeCount, 1, bigNumber);
		CheckRange(outputCount, 1, bigNumber);
		CheckRange(inputCount + constantCount, 0, storageSlots + 1);
		CheckRange(outputCount + constantCount, 0, storageSlots + 1);
		constantValues.resize(constantCount);
		nodes.resize(constantCount + inputCount + compositeCount + outputCount);
		auto link = [this](Node &node, int32_t sourceIndex, Link::LinkType linkType) {
			auto &source = sources[sourceIndex];
			source.uses += 1;
			auto nodeIndex = source.nodeIndex;
			auto &linkedNode = nodes[nodeIndex];
			auto linkIndex = links.size();
			Link &link = links.emplace_back();
			link.type = linkType;
			link.neighbours[Link::directionUp].nodeIndex = nodeIndex;
			link.neighbours[Link::directionDown].nodeIndex = &node - &nodes[0];
			link.neighbours[Link::directionUp].linkIndicesIndex = linkedNode.linkIndices[Link::directionDown].size();
			link.neighbours[Link::directionDown].linkIndicesIndex = node.linkIndices[Link::directionUp].size();
			link.upstreamOutputIndex = source.outputIndex;
			linkedNode.linkIndices[Link::directionDown].push_back(linkIndex);
			node.linkIndices[Link::directionUp].push_back(linkIndex);
		};
		auto presentSource = [this](int32_t nodeIndex, int32_t outputIndex) {
			nodes[nodeIndex].sources.push_back(int32_t(sources.size()));
			sources.push_back({ nodeIndex, outputIndex });
		};
		auto seenLsnsLife3 = false;
		for (int32_t constantIndex = 0; constantIndex < constantCount; ++constantIndex)
		{
			auto nodeIndex = constantIndex;
			auto &constant = nodes[nodeIndex];
			constant.type = Node::constant;
			auto &constantValue = constantValues[constantIndex];
			constantValue = newConstantValues[constantIndex];
			if (constantValue == lsnsLife3Value)
			{
				seenLsnsLife3 = true;
			}
			presentSource(nodeIndex, 0);
		}
		CheckRange(seenLsnsLife3 ? 1 : 0, 1, 2);
		inputStorageSlots.resize(inputCount);
		inputInitials.resize(inputCount);
		for (int32_t inputIndex = 0; inputIndex < inputCount; ++inputIndex)
		{
			auto nodeIndex = constantCount + inputIndex;
			auto &input = nodes[nodeIndex];
			input.type = Node::input;
			auto &inputStorageSlot = inputStorageSlots[inputIndex];
			inputStorageSlot = newInputStorageSlots[inputIndex];
			auto &inputInitial = inputInitials[inputIndex];
			inputInitial = newInputInitials[inputIndex];
			CheckRange(inputStorageSlot, 0, storageSlots);
			presentSource(nodeIndex, 0);
		}
		for (int32_t compositeIndex = 0; compositeIndex < compositeCount; ++compositeIndex)
		{
			auto &protoComposite = newComposites[compositeIndex];
			auto nodeIndex = constantCount + inputCount + compositeIndex;
			auto &node = nodes[nodeIndex];
			if (auto *protoSelect = std::get_if<ProtoSelect>(&protoComposite))
			{
				node.type = Node::select;
				auto stageCount = int32_t(protoSelect->tmps.size()) + 1;
				auto laneCount2 = int32_t(protoSelect->sources.size()) - stageCount;
				CheckRange(laneCount2, 2, bigNumber);
				CheckRange(laneCount2 % 2, 0, 1);
				auto laneCount = laneCount2 / 2;
				CheckRange(stageCount, 2, bigNumber);
				node.workSlotsNeeded = stageCount + laneCount * 2;
				CheckRange(node.workSlotsNeeded, 1, workSlots + 1);
				node.tmps.resize(stageCount - 1);
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					auto nonzeroSource = protoSelect->sources[laneIndex * 2];
					auto zeroSource = protoSelect->sources[laneIndex * 2 + 1];
					CheckRange(nonzeroSource, 0, sources.size());
					CheckRange(zeroSource, 0, sources.size());
					link(node, nonzeroSource, Link::toSelectNonzero);
					link(node, zeroSource, Link::toSelectZero);
				}
				for (int32_t stageIndex = 0; stageIndex < stageCount; ++stageIndex)
				{
					if (stageIndex > 0)
					{
						auto tmp = protoSelect->tmps[stageIndex - 1];
						CheckRange(tmp, 0, tmpCount);
						node.tmps[stageIndex - 1] = tmp;
					}
					auto source = protoSelect->sources[laneCount * 2 + stageIndex];
					CheckRange(source, 0, sources.size());
					link(node, source, Link::toBinary);
				}
				for (int32_t laneIndex = 0; laneIndex < laneCount; ++laneIndex)
				{
					presentSource(nodeIndex, laneIndex);
				}
			}
			else if (auto *protoBinary = std::get_if<ProtoBinary>(&protoComposite))
			{
				node.type = Node::binary;
				node.tmps.resize(1);
				node.tmps[0] = protoBinary->tmp;
				auto rhsSource = protoBinary->rhsSource;
				auto lhsSource = protoBinary->lhsSource;
				CheckRange(node.tmps[0], 0, tmpCount);
				CheckRange(rhsSource, 0, sources.size());
				CheckRange(lhsSource, 0, sources.size());
				link(node, rhsSource, Link::toBinary);
				link(node, lhsSource, Link::toBinary);
				node.workSlotsNeeded = 2;
				presentSource(nodeIndex, 0);
			}
		}
		outputLinks.resize(outputCount);
		for (int32_t outputIndex = 0; outputIndex < outputCount; ++outputIndex)
		{
			auto nodeIndex = constantCount + inputCount + compositeCount + outputIndex;
			auto &node = nodes[nodeIndex];
			CheckRange(newOutputLinks[outputIndex].source, 0, sources.size());
			if (newOutputLinks[outputIndex].storageSlot < 0)
			{
				WorkSlotOutputLink outputLink;
				auto &outputSource = outputLink.sourceIndex;
				auto &outputWorkSlot = outputLink.workSlot;
				outputSource = newOutputLinks[outputIndex].source;
				CheckRange(newOutputLinks[outputIndex].storageSlot, -workSlots, 0);
				outputWorkSlot = -newOutputLinks[outputIndex].storageSlot - 1;
				outputLinks[outputIndex] = outputLink;
			}
			else
			{
				StorageSlotOutputLink outputLink;
				auto &outputSource = outputLink.sourceIndex;
				auto &outputStorageSlot = outputLink.storageSlot;
				outputSource = newOutputLinks[outputIndex].source;
				outputStorageSlot = newOutputLinks[outputIndex].storageSlot - 1;
				CheckRange(outputStorageSlot, 0, storageSlots);
				outputLinks[outputIndex] = outputLink;
			}
			node.type = Node::output;
			link(node, newOutputLinks[outputIndex].source, Link::toOutput);
		}
		clobberStorageSlots.resize(newClobberStorageSlots.size());
		for (int32_t clobberIndex = 0; clobberIndex < int32_t(clobberStorageSlots.size()); ++clobberIndex)
		{
			clobberStorageSlots[clobberIndex] = newClobberStorageSlots[clobberIndex];
			CheckRange(clobberStorageSlots[clobberIndex], 0, storageSlots);
		}
		voidStorageSlots.resize(newVoidStorageSlots.size());
		for (int32_t voidIndex = 0; voidIndex < int32_t(voidStorageSlots.size()); ++voidIndex)
		{
			voidStorageSlots[voidIndex] = newVoidStorageSlots[voidIndex];
			CheckRange(voidStorageSlots[voidIndex], 0, storageSlots);
		}
	}
}
