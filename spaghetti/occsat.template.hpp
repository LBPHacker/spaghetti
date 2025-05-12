#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

#define OCSSAT_SOLVERS(X) \
	@OCSSAT_SOLVERS@ \
	// last line of the macro, don't remove

namespace Spaghetti
{
	struct Problem
	{
		int32_t varCount = 0;
		std::vector<std::vector<int32_t>> clauses;
	};

	struct Solution
	{
		std::optional<std::vector<int32_t>> satisfiable;
	};

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

#define OCSSAT_SOLVER_FACTORY(name) static std::shared_ptr<Solver> Make_ ## name();
		OCSSAT_SOLVERS(OCSSAT_SOLVER_FACTORY)
#undef OCSSAT_SOLVER_FACTORY
	};
}
