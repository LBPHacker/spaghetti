#include "Solver.hpp"
#include <cassert>

namespace Spaghetti::Occsat
{
	void Solver::Dispatch()
	{
		assert(!dispatched);
		ready = false;
		solution.reset();
		cancelRequest = false;
		dispatched = true;
		thr = std::thread([this]() {
			Thread();
			ready = true;
		});
	}

	void Solver::Wait()
	{
		if (dispatched)
		{
			thr.join();
			dispatched = false;
		}
	}

	void Solver::Cancel()
	{
		{
			std::lock_guard lk(cancelRequestMx);
			cancelRequest = true;
		}
		cancelRequestCv.notify_all();
		Wait();
	}

	void Solver::SetProblem(std::shared_ptr<const Problem> newProblem)
	{
		problem = newProblem;
	}

	Solver::~Solver()
	{
		Cancel();
	}
}
