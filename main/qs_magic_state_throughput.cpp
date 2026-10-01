/*
 *  author: Suhas Vittal
 *  date:   7 April 2026
 * */

#include "argparse/argparse.h"
#include "globals.h"
#include "sim/configuration/allocator.h"
#include "sim/production.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string regime;
    int64_t    cycle_time_ns;
    int64_t    footprint;

    bool test_compact;

    ARGPARSE()
        .required("regime", "regime of evaluation (G or T)", regime)
        .required("cycle-time-ns", "Syndrome extraction cycle time in nanoseconds", cycle_time_ns)
        .required("max-footprint", "Max number of physical qubits for allocation", footprint)
        .optional("", "--test-compact-15to1", "Test compact 15:1 distillation", test_compact, false)
        .parse(argc, argv);

    std::vector<sim::ProducerQuantum> quanta;
    if (regime == "G")
    {
        quanta.push_back(sim::t_cc_cultivation(5));
    }
    else if (regime == "T")
    {
        sim::ProducerQuantum l1 = sim::t_cc_cultivation(3);
        sim::ProducerQuantum l2 = test_compact 
                                    ? sim::t_15to1_distillation_compact(25, 11, 11, l1.infidelity)
                                    : sim::t_15to1_distillation(25, 11, 11, l1.infidelity);
        quanta.push_back(l1);
        quanta.push_back(l2);
    }

    const double freq_khz = sim::compute_freq_khz(cycle_time_ns);
    auto alloc = sim::configuration::throughput_aware_allocation(footprint, quanta, freq_khz);

    print_stat_line(std::cout, "FOOTPRINT", alloc.physical_qubit_count);
    print_stat_line(std::cout, "THROUGHPUT", alloc.estimated_throughput);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
