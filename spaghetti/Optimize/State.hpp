#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <vector>

namespace Spaghetti::Optimize
{
	class Design;

	class State
	{
	public:
		struct Move
		{
			int32_t nodeIndex = -1;
			int32_t layerIndex2 = -1;
		};

	private:
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

	std::ostream &operator <<(std::ostream &stream, const State &state);
}
