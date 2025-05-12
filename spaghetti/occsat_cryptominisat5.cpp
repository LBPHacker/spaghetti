#include "occsat.hpp"
#include <cryptominisat5/cryptominisat.h>

namespace Spaghetti
{
	struct Cryptominisat5Solver : public Solver
	{
		void Thread() override
		{
			bool solverReady = false;
			CMSat::SATSolver solver;
			solver.set_single_run();
			solver.set_num_threads(threadCount);
			solver.new_vars(problem->varCount);
			std::vector<CMSat::Lit> cmsClause;
			for (auto &clause : problem->clauses)
			{
				for (auto value : clause)
				{
					if (value > 0)
					{
						cmsClause.push_back(CMSat::Lit(value - 1, false));
					}
					else
					{
						cmsClause.push_back(CMSat::Lit((-value) - 1, true));
					}
				}
				solver.add_clause(cmsClause);
				cmsClause.clear();
			}
			auto cmsThr = std::thread([this, &solver, &solverReady]() {
				if (solver.solve() == CMSat::l_True)
				{
					auto newSolution = std::make_shared<Solution>();
					newSolution->satisfiable.emplace();
					auto cmsSolution = solver.get_model();
					int32_t index = 0;
					for (auto value : cmsSolution)
					{
						index += 1;
						if (value == CMSat::l_True)
						{
							newSolution->satisfiable->push_back(index);
						}
						else
						{
							newSolution->satisfiable->push_back(-index);
						}
					}
					solution = newSolution;
				}
				{
					std::lock_guard lk(cancelRequestMx);
					if (!cancelRequest && !solution)
					{
						solution = std::make_shared<Solution>();
					}
					solverReady = true;
				}
				cancelRequestCv.notify_all();
			});
			bool shouldInterruptAsap = false;
			{
				std::unique_lock lk(cancelRequestMx);
				cancelRequestCv.wait(lk, [this, &solverReady]() {
					return solverReady || cancelRequest;
				});
				shouldInterruptAsap = !solverReady;
			}
			if (shouldInterruptAsap)
			{
				solver.interrupt_asap();
			}
			cmsThr.join();
		}
	};

	std::shared_ptr<Solver> Solver::Make_cryptominisat5()
	{
		return std::make_shared<Cryptominisat5Solver>();
	}
}
