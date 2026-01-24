#pragma once
#include "Solvers.hpp"
#include "Problem.hpp"
#include "Solution.hpp"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace Spaghetti::Occsat
{
	class Solver
	{
	protected:
		bool dispatched = false;
		bool cancelRequest = false;
		std::mutex cancelRequestMx;
		std::condition_variable cancelRequestCv;
		std::atomic<bool> ready = false;
		std::thread thr;
		std::shared_ptr<const Problem> problem;
		std::shared_ptr<const Solution> solution;
		uint32_t threadCount = 0;

		virtual void Thread() = 0;

	public:
		virtual ~Solver();

		void SetThreadCount(uint32_t newThreadCount)
		{
			threadCount = newThreadCount;
		}

		void SetProblem(std::shared_ptr<const Problem> newProblem);
		void Dispatch();
		void Cancel();
		void Wait();

		bool Ready() const
		{
			return ready;
		}

		bool Dispatched() const
		{
			return dispatched;
		}

		std::shared_ptr<const Solution> GetSolution() const
		{
			return solution;
		}

#define OccsatSolverFactory(name) static std::shared_ptr<Solver> Make ## name();
		OccsatSolvers(OccsatSolverFactory)
#undef OccsatSolverFactory
	};
}
