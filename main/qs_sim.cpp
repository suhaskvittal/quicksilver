/* author: Suhas Vittal
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

std::vector<sim::T_FACTORY*>
factory_init(std::vector<size_t> fact_num_15to1_by_level, uint64_t t_round_ns, size_t buffer_capacity)
{
    std::vector<sim::T_FACTORY*> t_factories;
    std::vector<sim::T_FACTORY*> prev_level;
    for (size_t i = 0; i < fact_num_15to1_by_level.size(); i++)
    {
        std::vector<sim::T_FACTORY*> curr_level;
        for (size_t j = 0; j < fact_num_15to1_by_level[i]; j++)
        {
            sim::T_FACTORY* f = new sim::T_FACTORY
                                {
                                    sim::T_FACTORY::f15to1(i, t_round_ns, buffer_capacity)
                                };
            f->resource_producers = prev_level;
            t_factories.push_back(std::move(f));
            curr_level.push_back(f);
        }
        prev_level = std::move(curr_level);
    }
    return t_factories;
}

std::vector<sim::MEMORY_MODULE*>
memory_init(std::vector<size_t> mem_module_capacities, std::vector<uint64_t> mem_sext_round_ns)
{
    std::vector<sim::MEMORY_MODULE*> mem_modules;
    for (size_t i = 0; i < mem_module_capacities.size(); i++)
    {
        size_t cap = mem_module_capacities[i];
        uint64_t round_ns = mem_sext_round_ns[i];
        double freq_ghz = sim::compute_freq_khz(round_ns, 1);

        sim::MEMORY_MODULE* m = new sim::MEMORY_MODULE(freq_ghz, cap);
        mem_modules.push_back(std::move(m));
    }
    return mem_modules;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int 
main(int argc, char** argv)
{
    /*
        Default configuration gives:
            1. A total memory capacity of 288+8 = 296 qubits. 288 qubits are in QLDPC memory, and 8 qubits are in the compute code.
            2. 4+1 15-to-1 magic state factories.
    */

    // simulation config:
    uint64_t            inst_sim{100'000};

    // compute config:
    size_t              cmp_num_rows{2};
    size_t              cmp_patches_per_row{4};
    uint64_t            cmp_sext_round_ns{1200};  // syndrome extraction latency

    // magic state config:
    std::vector<size_t> fact_num_15to1_by_level{4,1};
    size_t              fact_buffer_capacity{4};

    // memory config: these are based off the [[288, 12, 18]] BB code
    std::vector<size_t> mem_module_capacities(24,12);  // 24 x 12 
    std::vector<uint64_t> mem_sext_round_ns(24,1500);

    // parse input arguments;
    std::string trace_file{argv[1]};

    // Setup factories:
    std::vector<sim::T_FACTORY*> t_factories = factory_init(fact_num_15to1_by_level, cmp_sext_round_ns, fact_buffer_capacity);

    // Setup memory:
    std::vector<sim::MEMORY_MODULE*> mem_modules = memory_init(mem_module_capacities, mem_sext_round_ns);

    // Setup compute:
    sim::COMPUTE::CONFIG cfg;
    cfg.client_trace_files.push_back(trace_file);
    cfg.num_rows = cmp_num_rows;
    cfg.patches_per_row = cmp_patches_per_row;

    double cmp_freq_ghz = sim::compute_freq_khz(cmp_sext_round_ns, COMPUTE_CODE_DISTANCE);
    sim::COMPUTE* cmp = new sim::COMPUTE(cmp_freq_ghz, cfg, t_factories, mem_modules);

    // setup clock for all components:
    std::vector<sim::CLOCKABLE*> clockables{cmp};
    clockables.insert(clockables.end(), t_factories.begin(), t_factories.end());
    clockables.insert(clockables.end(), mem_modules.begin(), mem_modules.end());
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

    for (auto* m : mem_modules)
        delete m;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////