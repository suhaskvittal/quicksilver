/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#include "sim.h"

#include <iomanip>
#include <iostream>
#include <string_view>
#include <unordered_map>

constexpr size_t COMPUTE_CODE_DISTANCE = 27;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::string name, T value, bool indent=true)
{
    if (indent)
        name = "\t" + name;
    std::cout << std::setw(52) << std::left << name << " : " << std::setw(12) << std::right << value << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int 
main(int argc, char** argv)
{
    uint64_t            inst_sim{100'000};
    size_t              num_rows{16};
    size_t              patches_per_row{16};
    std::vector<size_t> num_15to1_by_level{4,1};
    uint64_t            compute_sext_round_ns{1200};  // syndrome extraction latency

    // parse input arguments;
    std::string trace_file{argv[1]};

    // Setup factories:
    size_t patch_idx{0};
    std::vector<sim::T_FACTORY*> t_factories;
    std::vector<sim::T_FACTORY*> prev_level;
    for (size_t i = 0; i < num_15to1_by_level.size(); i++)
    {
        std::vector<sim::T_FACTORY*> curr_level;
        for (size_t j = 0; j < num_15to1_by_level[i]; j++)
        {
            sim::T_FACTORY* f = new sim::T_FACTORY
                                {
                                    sim::T_FACTORY::f15to1(i, compute_sext_round_ns, 4, patch_idx++)
                                };
            f->resource_producers = prev_level;
            t_factories.push_back(std::move(f));
            curr_level.push_back(f);
        }
        prev_level = std::move(curr_level);
    }

    // Setup compute:
    sim::COMPUTE::CONFIG cfg;
    cfg.client_trace_files.push_back(trace_file);
    cfg.num_rows = num_rows;
    cfg.patches_per_row = patches_per_row;

    sim::COMPUTE* cmp = new sim::COMPUTE(compute_sext_round_ns, COMPUTE_CODE_DISTANCE, cfg, t_factories);

    // setup clock for all components:
    std::vector<sim::CLOCKABLE*> clockables{cmp};
    clockables.insert(clockables.end(), t_factories.begin(), t_factories.end());
    sim::setup_clk_scale_for_group(clockables);

    // start simulation:
    uint64_t tick{0};
    bool done;
    do
    {
        if (tick % 100'000 == 0)
        {
            if (tick % 5'000'000 == 0)
            {
                std::cout << "\nCMP CYCLE @ " << cmp->current_cycle() << " [";
                for (const auto& c : cmp->clients())
                {
                    uint64_t inst_done_k = c->s_inst_done / 1000;
                    std::cout << std::setw(4) << std::right << inst_done_k << "K";
                }
                std::cout << " ]\t";
            }
            std::cout << ".";
            std::cout.flush();
        }
        // tick all components in the system:
        for (auto* c : clockables)
            c->tick();

        // check if we are done:
        const auto& clients = cmp->clients();
        done = std::all_of(clients.begin(), clients.end(),
                            [&inst_sim] (const auto& c) { return c->s_inst_done >= inst_sim; });

        tick++;
    }
    while (!done);

    // print stats for each client:
    std::cout << "\n\nSIMULATION_STATS------------------------------------------------------------\n";
    double execution_time = (cmp->current_cycle() / cmp->freq_khz_) * 1e-3 / 60.0;

    print_stat_line("TOTAL_CYCLES", cmp->current_cycle(), false);
    print_stat_line("COMPUTE_SPEED (KHz)", cmp->freq_khz_, false);
    print_stat_line("EXECUTION_TIME (min)", execution_time, false);
    
    const auto& clients = cmp->clients();
    for (size_t i = 0; i < clients.size(); i++)
    {
        const auto& c = clients[i];
        std::cout << "CLIENT_" << i << "\n";

        print_stat_line("VIRTUAL_INST_DONE", c->s_inst_done);
        print_stat_line("UNROLLED_INST_DONE", c->s_unrolled_inst_done);
        print_stat_line("CYCLES_STALLED", c->s_cycles_stalled);
        print_stat_line("CYCLES_STALLED_BY_MEM", c->s_cycles_stalled_by_mem);
        print_stat_line("CYCLES_STALLED_BY_ROUTING", c->s_cycles_stalled_by_routing);
        print_stat_line("CYCLES_STALLED_BY_RESOURCE", c->s_cycles_stalled_by_resource);
    }

    // print factory stats:
    std::unordered_map<size_t, std::vector<sim::T_FACTORY*>> factory_level_map;
    for (auto* f : t_factories)
        factory_level_map[f->level].push_back(f);

    for (size_t i = 0; i < factory_level_map.size(); i++)
    {
        std::cout << "FACTORY_L" << i << "\n";

        uint64_t tot_prod_tries{0},
                 tot_failures{0};
        for (auto* f : factory_level_map[i])
        {
            tot_prod_tries += f->s_prod_tries;
            tot_failures += f->s_failures;
        }

        print_stat_line("PROD_TRIES", tot_prod_tries);
        print_stat_line("FAILURES", tot_failures);
    }

    // deallocate memory:
    delete cmp;
    for (auto* f : t_factories)
        delete f;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////