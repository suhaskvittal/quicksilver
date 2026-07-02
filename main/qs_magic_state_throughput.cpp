/*
 *  author: Suhas Vittal
 *  date:   7 April 2026
 * */

#include "argparse/argparse.h"
#include "globals.h"
#include "sim/configuration/allocator.h"
#include "sim/configuration/allocator/impl.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using FactorySpecification = sim::configuration::FactorySpecification;

constexpr FactorySpecification CULTIVATION_D3
{
    .is_cultivation=true,
    .cycle_time_ns=1200,
    .buffer_capacity=1,
    .output_error_rate=1e-6,
    .escape_distance=13,
    .rounds=18,
    .probability_of_success=0.2
};

constexpr FactorySpecification CULTIVATION_D5
{
    .is_cultivation=true,
    .cycle_time_ns=1200,
    .buffer_capacity=1,
    .output_error_rate=1e-8,
    .escape_distance=15,
    .rounds=25,
    .probability_of_success=0.02
};

constexpr FactorySpecification DISTILLATION_15_TO_1
{
    .is_cultivation=false,
    .cycle_time_ns=1200,
    .buffer_capacity=1,
    .output_error_rate=1e-12,
    .dx=25,
    .dz=11,
    .dm=11,
    .input_count=4,
    .output_count=1,
    .rotations=11
};

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string regime;
    int64_t    cycle_time_ns;
    int64_t    footprint;

    ARGPARSE()
        .required("regime", "regime of evaluation (G or T)", regime)
        .required("cycle-time-ns", "Syndrome extraction cycle time in nanoseconds", cycle_time_ns)
        .required("max-footprint", "Max number of physical qubits for allocation", footprint)
        .parse(argc, argv);

    std::vector<FactorySpecification> spec;
    if (regime == "G")
    {
        spec.push_back(CULTIVATION_D5);
    }
    else if (regime == "T")
    {
        spec.push_back(CULTIVATION_D3);
        spec.push_back(DISTILLATION_15_TO_1);
    }

    for (auto& s : spec)
        s.cycle_time_ns = cycle_time_ns;
    auto alloc = sim::configuration::allocate_magic_state_factories(footprint, spec);

    print_stat_line(std::cout, "FOOTPRINT", alloc.physical_qubit_count);
    print_stat_line(std::cout, "THROUGHPUT", alloc.estimated_throughput);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
