#include "optimize.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main()
{
	constexpr auto    temperatureInitial = 1.0;
	constexpr auto    temperatureFinal   = 0.95;
	constexpr auto    temperatureLoss    = 1e-7;
	constexpr int32_t iterationCount     = 100000;
	auto design = std::make_shared<Design>();
	try
	{
		std::cin >> *design;
	}
	catch (const StreamFailed &ex)
	{
		std::cerr << "failed to parse input: " << ex.what() << std::endl;
		return 2;
	}
	catch (const RangeCheckFailed &ex)
	{
		std::cerr << "failed to parse input: " << ex.what() << std::endl;
		return 2;
	}
	auto optimizer = std::make_shared<Optimizer>();
	optimizer->PokeState({ design->Initial(), temperatureInitial });
	std::random_device rd;
	optimizer->threadCount = std::thread::hardware_concurrency();
	optimizer->rng.seed(rd());
	std::cerr << *optimizer->PeekState().state;
	optimizer->Dispatch({ iterationCount, temperatureFinal, temperatureLoss });
	while (!optimizer->Ready())
	{
		auto ostate = optimizer->PeekState();
		std::cerr << "temperature: " << ostate.temperature << std::endl;
		std::cerr << *ostate.state;
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}
	auto ostate = optimizer->PeekState();
	std::cerr << "final temperature: " << ostate.temperature << std::endl;
	std::cerr << *ostate.state;
	std::shared_ptr<Plan> plan;
	try
	{
		plan = ostate.state->GetEnergy<EnergyWithPlan>().ToPlan();
	}
	catch (const EnergyWithPlan::ToPlanFailed &ex)
	{
		std::cerr << "design parameters not satisfied, no plan generated: " << ex.what() << std::endl;
		return 1;
	}
	std::cout << *plan;
	return 0;
}
