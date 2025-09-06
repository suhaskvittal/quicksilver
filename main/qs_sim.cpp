/* author: Suhas Vittal
    date:   26 August 2025
*/

#include "argparse.h"
#include "sim.h"

#include <iomanip>
#include <iostream>
#include <string_view>
#include <unordered_map>

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
memory_init(size_t num_modules, size_t banks_per_module, size_t qubits_per_bank, uint64_t t_round_ns, size_t code_distance)
{
    std::vector<sim::MEMORY_MODULE*> mem_modules;
    for (size_t i = 0; i < num_modules; i++)
    {
        double freq_khz = sim::compute_freq_khz(t_round_ns, code_distance);
        sim::MEMORY_MODULE* m = new sim::MEMORY_MODULE(freq_khz, qubits_per_bank, banks_per_module);
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
            1. A total memory capacity of 48*12 + 8 = 584 qubits. 576 qubits are in QLDPC memory, and 8 qubits are in the compute code.
            2. 4+1 15-to-1 magic state factories.
    */

    // simulation config:
    std::string traces;
    uint64_t    inst_sim{100'000};

    // compute config:
    size_t cmp_num_rows{1};
    size_t cmp_patches_per_row{8};
    size_t cmp_code_distance{19};
    uint64_t cmp_ext_round_ns{1200};

    // magic state config:
    size_t fact_num_15to1_L1{4};
    size_t fact_num_15to1_L2{1};
    size_t fact_buffer_capacity{4};

    // memory config:
    size_t mem_bb_num_modules{4};
    size_t mem_bb_banks_per_module{12};
    size_t mem_bb_qubits_per_bank{12};
    size_t mem_bb_code_distance{18};
    uint64_t mem_bb_ext_round_ns{1500};

    // parse input arguments;
    ARGPARSE pp(argc, argv);

    pp.read_required("traces", traces);
    pp.find_optional("sim", inst_sim);
    pp.find_optional("cmp_num_rows", cmp_num_rows);
    pp.find_optional("cmp_patches_per_row", cmp_patches_per_row);
    pp.find_optional("cmp_code_distance", cmp_code_distance);
    pp.find_optional("cmp_ext_round_ns", cmp_ext_round_ns);
    pp.find_optional("fact_15to1_L1", fact_num_15to1_L1);
    pp.find_optional("fact_15to1_L2", fact_num_15to1_L2);
    pp.find_optional("fact_buffer_cap", fact_buffer_capacity);
    pp.find_optional("mem_bb_modules", mem_bb_num_modules);
    pp.find_optional("mem_bb_banks_per_module", mem_bb_banks_per_module);
    pp.find_optional("mem_bb_qubits_per_bank", mem_bb_qubits_per_bank);
    pp.find_optional("mem_bb_code_distance", mem_bb_code_distance);
    pp.find_optional("mem_bb_ext_round_ns", mem_bb_ext_round_ns);

    // Setup factories:
    std::vector<sim::T_FACTORY*> t_factories = factory_init(
                                                        {fact_num_15to1_L1, fact_num_15to1_L2},
                                                        cmp_ext_round_ns,
                                                        fact_buffer_capacity);

    // Setup memory:
    std::vector<sim::MEMORY_MODULE*> mem_modules = memory_init(
                                                        mem_bb_num_modules, 
                                                        mem_bb_banks_per_module, 
                                                        mem_bb_qubits_per_bank, 
                                                        mem_bb_ext_round_ns, 
                                                        mem_bb_code_distance);

    // Setup compute:
    sim::COMPUTE::CONFIG cfg;

    // split `traces` by `;` delimiter
    std::string::iterator sc_it = std::find(traces.begin(), traces.end(), ';');
    std::string first_trace(traces.begin(), sc_it);
    cfg.client_trace_files.push_back(first_trace);

    while (sc_it != traces.end())
    {
        auto start_it = sc_it+1;
        sc_it = std::find(start_it, traces.end(), ';');
        std::string trace_part(start_it, sc_it);
        cfg.client_trace_files.push_back(trace_part);
    }

    std::cout << "clients:\n";
    for (size_t i = 0; i < cfg.client_trace_files.size(); i++)
        std::cout << "\tclient " << i << ": " << cfg.client_trace_files[i] << "\n";
    
    cfg.code_distance = cmp_code_distance;
    cfg.num_rows = cmp_num_rows;
    cfg.patches_per_row = cmp_patches_per_row;

    double cmp_freq_ghz = sim::compute_freq_khz(cmp_ext_round_ns, cmp_code_distance);
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