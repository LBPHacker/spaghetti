#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <shared_mutex>
#include <stdexcept>
#include <thread>
#include <variant>
#include <vector>

constexpr int32_t tmpCount = 12;

enum LinkDirection
{
	linkUpstream,
	linkDownstream,
	linkMax,
};

struct Link
{
	enum LinkType
	{
		toBinary,
		toSelectNonzero,
		toSelectZero,
		toOutput,
	} type;
	struct Direction
	{
		int32_t nodeIndex = -1;
		int32_t linkIndicesIndex = -1;
	};
	std::array<Direction, linkMax> directions;
	int32_t upstreamOutputIndex = -1;
};

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
	std::array<std::vector<int32_t>, linkMax> linkIndices;
	std::vector<int32_t> tmps;
	int32_t workSlotsNeeded = -1;
	std::vector<int32_t> sources;
};

struct Source
{
	int32_t nodeIndex;
	int32_t outputIndex;
	int32_t uses = 0;
};

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
	std::vector<int32_t> clobberStorageSlots;
	std::vector<int32_t> voidStorageSlots;
	struct OutputLink
	{
		int32_t sourceIndex;
		int32_t storageSlot;
	};
	std::vector<OutputLink> outputLinks;
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

struct Move
{
	int32_t nodeIndex = -1;
	int32_t layerIndex2 = -1;
};

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
		static constexpr int32_t cost = 1;
	};

	struct Bottom : public StepBase
	{
		static constexpr int32_t cost = 4;
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

	bool outputRemapFailed = false;
	void MoveWorkSlot0GroupsBack();
	void SortSteps();

public:
	struct ToPlanFailed : public std::runtime_error
	{
		using runtime_error::runtime_error;
	};
	struct OutputRemappingFailed : public ToPlanFailed
	{
		OutputRemappingFailed() : ToPlanFailed("output remapping failed")
		{
		}
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

class State
{
	int32_t iteration;
	std::shared_ptr<const Design> design;
	std::vector<int32_t> nodeIndices;
	std::vector<int32_t> layers;

	int32_t LayerSize(int32_t layerIndex) const;
	std::vector<int32_t> InsertNode(int32_t layerIndex, int32_t extraNodeIndex) const;
	std::vector<int32_t> NodeIndexToLayerIndex() const;
	std::vector<Move> PossibleMoves() const;
	std::optional<Move> RandomValidMove(std::mt19937_64 &rng) const;
	int32_t LayerBegins(int32_t layerIndex) const;

public:
	State() = default;
	std::shared_ptr<State> RandomNeighbour(std::mt19937_64 &rng) const;

	template<class EnergyType>
	EnergyType GetEnergy() const;

	const Design *GetDesign() const
	{
		return design.get();
	}

	const std::vector<int32_t> &GetLayers() const
	{
		return layers;
	}

	friend class Design;
	friend std::ostream &operator <<(std::ostream &stream, const State &state);
};

struct StreamFailed : public std::runtime_error
{
	using runtime_error::runtime_error;
};
struct RangeCheckFailed : public std::invalid_argument
{
	using invalid_argument::invalid_argument;
};

std::ostream &operator <<(std::ostream &stream, const State &state);

struct Schedule
{
	struct Step
	{
		int64_t beginsAt;
		double temperature;
	};
	std::vector<Step> steps;
	int64_t endsAt = 0;

	int64_t NormalizeProgress(int64_t progress) const;
};

struct OptimizerState
{
	std::shared_ptr<const State> state;
	std::shared_ptr<const Schedule> schedule;
	int64_t progress = 0;

	bool Done() const;
	double Temperature() const;
};
OptimizerState OptimizeOnce(std::mt19937_64 &rng, OptimizerState os, int32_t iterationCount);

class Optimizer
{
	bool dispatched = false;
	std::atomic<bool> cancelRequest = false;
	std::atomic<bool> ready = false;
	std::thread thr;

	void ThreadFunc();
	OptimizerState heldState;
	std::shared_mutex stateMx;

public:
	uint32_t threadCount = 1;
	std::mt19937_64 rng;

	struct DispatchParameters
	{
		int32_t iterationCount;
		int32_t roundsPerExchange;
	};
	void Dispatch(DispatchParameters dp);
	void Wait();
	void Cancel();

	OptimizerState PeekState();
	void PokeState(OptimizerState newState);

	bool Dispatched() const
	{
		return dispatched;
	}

	bool Ready() const
	{
		return ready;
	}

	~Optimizer();
};
