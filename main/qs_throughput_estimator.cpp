/*
 *  author: Claude
 *  date:   15 January 2026
 * */

#include "argparse.h"
#include "globals.h"
#include "sim.h"
#include "sim/configuration/allocator/impl.h"
#include "sim/configuration/predefined_ed_protocols.h"
#include "sim/operable.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Simulation state:
 * */
cycle_type SIM_CURRENT_CYCLE{0};
uint64_t   SIM_RESOURCES_CONSUMED{0};

/*
 * Simulation functions:
 * */
void sim_init(sim::configuration::ALLOCATION&);
void sim_tick(sim::configuration::ALLOCATION&);
void sim_cleanup(sim::configuration::ALLOCATION&);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    int64_t     physical_qubit_budget;
    int64_t     sim_cycles;
    int64_t     epr_protocol_id;
    std::string production_type;

    ARGPARSE()
        .optional("-q", "--budget",  "Physical qubit budget",         physical_qubit_budget, 12'000)
        .optional("-c", "--cycles",  "Number of simulation cycles",   sim_cycles,            1'000'000)
        .optional("-t", "--type",    "Production type (magic, epr)",  production_type,       std::string("magic"))

        .optional("", "--epr-protocol-id", "ED Protocol ID", epr_protocol_id, 3)

        .parse(argc, argv);

    sim::configuration::ALLOCATION alloc;

    if (production_type == "magic")
    {
        /*
         * L1: d = 3 color code cultivation
         * L2: 15:1 (dx,dz,dm) = (25,11,11) distillation
         * */
        sim::configuration::FACTORY_SPECIFICATION l1_spec
        {
            .is_cultivation=true,
            .syndrome_extraction_round_time_ns=1200,
            .buffer_capacity=1,
            .output_error_rate=1e-6,
            .escape_distance=13,
            .rounds=18,
            .probability_of_success=0.2
        };

        sim::configuration::FACTORY_SPECIFICATION l2_spec
        {
            .is_cultivation=false,
            .syndrome_extraction_round_time_ns=1200,
            .buffer_capacity=2,
            .output_error_rate=1e-12,
            .dx=25,
            .dz=11,
            .dm=11,
            .input_count=4,
            .output_count=1,
            .rotations=11
        };

        alloc = sim::configuration::allocate_magic_state_factories(physical_qubit_budget, {l1_spec, l2_spec});
    }
    else if (production_type == "epr")
    {
        std::vector<sim::configuration::ED_SPECIFICATION> specs;
        switch (epr_protocol_id)
        {
        case 0:
            specs = sim::configuration::ed::protocol_0(1200000, 1);
            break;
        case 1:
            specs = sim::configuration::ed::protocol_1(1200000, 1);
            break;
        case 2:
            specs = sim::configuration::ed::protocol_2(1200000, 1);
            break;
        case 3:
            specs = sim::configuration::ed::protocol_3(1200000, 1);
            break;
        case 4:
            specs = sim::configuration::ed::protocol_4(1200000, 1);
            break;
        case 5:
            specs = sim::configuration::ed::protocol_5(1200000, 1);
            break;
        }

        alloc = sim::configuration::allocate_entanglement_distillation_units(physical_qubit_budget, specs);
    }
    else
    {
        std::cerr << "unknown production type: " << production_type << "\n"
                  << "valid options: magic, epr\n";
        return 1;
    }

    for (size_t i = 0; i < alloc.producers.size(); i++)
    {
        std::cout << "L" << i+1 << " production count: " << alloc.producers[i].size() << "\n";
    }

    /*
     * Run simulation:
     * */
    sim_init(alloc);
    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();

    while (SIM_CURRENT_CYCLE < sim_cycles)
    {
        sim_tick(alloc);
        SIM_CURRENT_CYCLE++;
    }

    /*
     * Compute true throughput:
     * */
    auto& last_level = alloc.producers.back();
    const double simulated_time_s = last_level[0]->current_cycle() / (last_level[0]->freq_khz * 1e3);
    const double true_throughput  = static_cast<double>(SIM_RESOURCES_CONSUMED) / simulated_time_s;

    /*
     * Print statistics:
     * */
    print_stat_line(std::cout, "PRODUCTION_TYPE",         production_type);
    print_stat_line(std::cout, "PHYSICAL_QUBIT_BUDGET",   physical_qubit_budget);
    print_stat_line(std::cout, "PHYSICAL_QUBIT_OVERHEAD", alloc.physical_qubit_count);

    for (size_t i = 0; i < alloc.producers.size(); i++)
    {
        const std::string label = "L" + std::to_string(i + 1);
        sim::print_stats_for_factories(std::cout, label, alloc.producers[i]);
    }

    print_stat_line(std::cout, "SIMULATION_CYCLES",               SIM_CURRENT_CYCLE);
    print_stat_line(std::cout, "RESOURCES_CONSUMED",              SIM_RESOURCES_CONSUMED);
    print_stat_line(std::cout, "ESTIMATED_THROUGHPUT_PER_SECOND", alloc.estimated_throughput);
    print_stat_line(std::cout, "TRUE_THROUGHPUT_PER_SECOND",      true_throughput);

    sim_cleanup(alloc);
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
sim_init(sim::configuration::ALLOCATION& alloc)
{
    std::vector<sim::OPERABLE*> operables;
    for (auto& level : alloc.producers)
        for (auto* p : level)
            operables.push_back(p);
    sim::coordinate_clock_scale(operables);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
sim_tick(sim::configuration::ALLOCATION& alloc)
{
    for (auto& level : alloc.producers)
        for (auto* p : level)
            p->tick();

    // consume resources from the final production level
    for (auto* p : alloc.producers.back())
    {
        size_t avail = p->buffer_occupancy();
        p->consume(avail);
        SIM_RESOURCES_CONSUMED += avail;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
sim_cleanup(sim::configuration::ALLOCATION& alloc)
{
    for (auto& level : alloc.producers)
        for (auto* p : level)
            delete p;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anon
