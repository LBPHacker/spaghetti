#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <random>
#include <shared_mutex>
#include <thread>

namespace Spaghetti::Optimize
{
	class State;
	struct Schedule;

	class Optimizer
	{
	public:
		struct DispatchParameters
		{
			int32_t iterationCount;
			int32_t roundsPerExchange;
		};

	private:
		struct State
		{
			std::shared_ptr<const Optimize::State> state;
			std::shared_ptr<const Optimize::Schedule> schedule;
			int64_t progress = 0;

			bool Done() const;
			double Temperature() const;
		};

		struct ThreadContext
		{
			std::mt19937_64 rng;
			std::thread thr;
			State ostate;
			bool threadWorking = false;
			bool threadExit = false;
			std::mutex threadStateMx;
			std::condition_variable threadStateCv;

			void ThreadFunc(DispatchParameters dp);
			void Start();
			void Exit();
			void Wait();
		};

		bool dispatched = false;
		std::atomic<bool> cancelRequest = false;
		std::atomic<bool> ready = false;
		std::thread thr;

		void ThreadFunc();
		State heldState;
		std::shared_mutex stateMx;

	public:
		uint32_t threadCount = 1;
		std::mt19937_64 rng;

		static State OptimizeOnce(std::mt19937_64 &rng, State os, int32_t iterationCount);

		void Dispatch(DispatchParameters dp);
		void Wait();
		void Cancel();

		State PeekState();
		void PokeState(State newState);

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
}
