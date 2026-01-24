#include "Optimizer.hpp"
#include "Energy.hpp"
#include "Schedule.hpp"
#include "State.hpp"
#include <cassert>

namespace Spaghetti::Optimize
{
	bool Optimizer::State::Done() const
	{
		return progress >= schedule->endsAt;
	}

	double Optimizer::State::Temperature() const
	{
		auto it = std::lower_bound(schedule->steps.begin(), schedule->steps.end(), progress, [](auto &step, auto progress) {
			return step.beginsAt <= progress;
		});
		if (it == schedule->steps.end())
		{
			return schedule->steps.back().temperature;
		}
		auto &stepLeft = *(it - 1);
		auto &stepRight = *it;
		return stepLeft.temperature + (stepRight.temperature - stepLeft.temperature) * (progress - stepLeft.beginsAt) / (stepRight.beginsAt - stepLeft.beginsAt);
	}

	namespace
	{
		double TransitionProbability(double energy, double newEnergy, double temperature)
		{
			if (newEnergy < energy)
			{
				return 1.0;
			}
			return std::exp(-(newEnergy - energy) / temperature);
		}
	}

	void Optimizer::ThreadContext::ThreadFunc(DispatchParameters dp)
	{
		while (true)
		{
			{
				std::unique_lock lk(threadStateMx);
				threadStateCv.wait(lk, [this]() {
					return threadWorking || threadExit;
				});
				if (threadExit)
				{
					break;
				}
			}
			ostate = OptimizeOnce(rng, ostate, dp.iterationCount);
			{
				std::unique_lock lk(threadStateMx);
				threadWorking = false;
			}
			threadStateCv.notify_all();
		}
	}

	void Optimizer::ThreadContext::Start()
	{
		{
			std::unique_lock lk(threadStateMx);
			threadWorking = true;
		}
		threadStateCv.notify_all();
	}

	void Optimizer::ThreadContext::Exit()
	{
		{
			std::unique_lock lk(threadStateMx);
			threadExit = true;
		}
		threadStateCv.notify_all();
	}

	void Optimizer::ThreadContext::Wait()
	{
		std::unique_lock lk(threadStateMx);
		threadStateCv.wait(lk, [this]() {
			return !threadWorking;
		});
	}

	void Optimizer::Dispatch(DispatchParameters dp)
	{
		assert(!dispatched);
		ready = false;
		cancelRequest = false;
		dispatched = true;
		thr = std::thread([this, dp]() {
			std::vector<ThreadContext> threadContexts(threadCount);
			for (auto &threadContext : threadContexts)
			{
				threadContext.rng.seed(rng());
				threadContext.thr = std::thread([&threadContext, dp]() {
					threadContext.ThreadFunc(dp);
				});
			}
			int32_t currentRound = 0;
			while (true)
			{
				auto shouldExchange = currentRound == 0;
				if (dp.roundsPerExchange > 0)
				{
					shouldExchange = currentRound % dp.roundsPerExchange == 0;
				}
				currentRound += 1;
				if (shouldExchange)
				{
					auto stateSample = PeekState();
					for (auto &threadContext : threadContexts)
					{
						threadContext.ostate = stateSample;
					}
				}
				for (auto &threadContext : threadContexts)
				{
					threadContext.Start();
				}
				for (auto &threadContext : threadContexts)
				{
					threadContext.Wait();
				}
				State newState;
				double stateLinear = 0;
				for (auto &threadContext : threadContexts)
				{
					auto threadStateLinear = threadContext.ostate.state->GetEnergy<Energy>().linear;
					if (!newState.state || stateLinear > threadStateLinear)
					{
						newState = threadContext.ostate;
						stateLinear = threadStateLinear;
					}
				}
				PokeState(newState);
				if (newState.Done())
				{
					break;
				}
				if (cancelRequest)
				{
					break;
				}
			}
			for (auto &threadContext : threadContexts)
			{
				threadContext.Exit();
				threadContext.thr.join();
			}
			ready = true;
		});
	}

	void Optimizer::Wait()
	{
		if (dispatched)
		{
			thr.join();
			dispatched = false;
		}
	}

	void Optimizer::Cancel()
	{
		cancelRequest = true;
		Wait();
	}

	Optimizer::~Optimizer()
	{
		Cancel();
	}

	Optimizer::State Optimizer::PeekState()
	{
		std::shared_lock lk(stateMx);
		return heldState;
	}

	void Optimizer::PokeState(State newState)
	{
		std::unique_lock lk(stateMx);
		heldState = newState;
	}

	Optimizer::State Optimizer::OptimizeOnce(std::mt19937_64 &rng, Optimizer::State os, int32_t iterationCount)
	{
		std::uniform_real_distribution<double> rdist(0.0, 1.0);
		auto energyLinear = os.state->GetEnergy<Energy>().linear;
		for (int32_t iterationIndex = 0; iterationIndex < iterationCount && !os.Done(); ++iterationIndex)
		{
			auto newState = os.state->RandomNeighbour(rng);
			auto newEnergyLinear = newState->GetEnergy<Energy>().linear;
			if (TransitionProbability(energyLinear, newEnergyLinear, os.Temperature()) >= rdist(rng))
			{
				os.state = newState;
				energyLinear = newEnergyLinear;
			}
			os.progress += 1;
		}
		return os;
	}
}
