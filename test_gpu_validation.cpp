// NOTE (mozhgan#1#07/12/16): We SHOULD have each BOOST_CHECK as a seperate Test case. The reason for this is if it fails one test, it never reach the next BOOST_CHECK that exist in the same TEST_CASE.

#define BOOST_TEST_DYN_LINK
#define BOOST_TEST_MODULE GPU_TestSuites

//#include "boost/test/unit_test.hpp"
#include <boost/test/included/unit_test.hpp>
#include "pop/AgentPopulation.h"
#include "sim/Simulation.h"
#include "gpu/CUDAAgentModel.h"

using namespace std;

BOOST_AUTO_TEST_SUITE(GPUTest) //name of the test suite is modelTest

BOOST_AUTO_TEST_CASE(SimulationNameCheck)
{
    ModelDescription flame_model("circles_model");
    AgentDescription circle_agent("circle");

    AgentFunctionDescription output_data("output_data");
    AgentFunctionOutput output_location("location");
    output_data.addOutput(output_location);

    circle_agent.addAgentFunction(output_data);
    flame_model.addAgent(circle_agent);

    Simulation simulation(flame_model);

    SimulationLayer output_layer(simulation, "output_layer");
    output_layer.addAgentFunction("output_data");
    simulation.addSimulationLayer(output_layer);

    CUDAAgentModel cuda_model(flame_model);
    cuda_model.setPopulationData(population);

    BOOST_TEST_MESSAGE( "\nTesting CUDA Agent model name" );
    BOOST_CHECK_MESSAGE(cuda_model.);

    cuda_model.simulate(simulation);



}

BOOST_AUTO_TEST_SUITE_END()

