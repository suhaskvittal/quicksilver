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
    double freq_khz = sim::compute_freq_khz(t_round_ns, code_distance);
    for (size_t i = 0; i < num_modules; i++)
    {
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
    int cmp_repl_id{0};

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
    pp.usage = "qs_sim [options] <traces>";

    pp.read_required("traces", traces);
    pp.find_optional("sim", inst_sim);
    pp.find_optional("cmp_num_rows", cmp_num_rows);
    pp.find_optional("cmp_patches_per_row", cmp_patches_per_row);
    pp.find_optional("cmp_code_distance", cmp_code_distance);
    pp.find_optional("cmp_ext_round_ns", cmp_ext_round_ns);
    pp.find_optional("cmp_repl_id", cmp_repl_id);
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

    // split `traces` by `;` delimiter
    std::vector<std::string> client_trace_files;
    std::string::iterator sc_it = std::find(traces.begin(), traces.end(), ';');
    std::string first_trace(traces.begin(), sc_it);
    client_trace_files.push_back(first_trace);

    while (sc_it != traces.end())
    {
        auto start_it = sc_it+1;
        sc_it = std::find(start_it, traces.end(), ';');
        std::string trace_part(start_it, sc_it);
        client_trace_files.push_back(trace_part);
    }

    std::cout << "clients:\n";
    for (size_t i = 0; i < client_trace_files.size(); i++)
        std::cout << "\tclient " << i << ": " << client_trace_files[i] << "\n";

    double cmp_freq_ghz = sim::compute_freq_khz(cmp_ext_round_ns, cmp_code_distance);
    sim::COMPUTE* cmp = new sim::COMPUTE(
                            cmp_freq_ghz, 
                            client_trace_files, 
                            cmp_num_rows, 
                            cmp_patches_per_row, 
                            t_factories, 
                            mem_modules,
                            static_cast<sim::COMPUTE::REPLACEMENT>(cmp_repl_id));

    // setup clock for all components:
    std::vector<sim::CLOCKABLE*> clockables{cmp};
    clockables.insert(clockables.end(), t_factories.begin(), t_factories.end());
    clockables.insert(clockables.end(), mem_modules.begin(), mem_modules.end());
    sim::setup_clk_scale_for_group(clockables);

    std::cout << "cmp frequency: " << cmp->freq_khz_ << " kHz\n";
    for (size_t i = 0; i < t_factories.size(); i++)
        std::cout << "factory " << i << " frequency: " << t_factories[i]->freq_khz_ << " kHz\n";
    for (size_t i = 0; i < mem_modules.size(); i++)
        std::cout << "memory module " << i << " frequency: " << mem_modules[i]->freq_khz_ << " kHz\n";


    // start simulation:
    uint64_t tick{0};
    bool done;
    do
    {
        sim::print_progress(std::cout, cmp, tick, 100'000, 5'000'000);
        // tick all components in the systm:
        for (auto* c : clockables)
            c->tick();

        // check if we are done:
        const auto& clients = cmp->clients();
        done = std::all_of(clients.begin(), clients.end(),
                            [&inst_sim] (const auto& c) { return c->s_inst_done >= inst_sim; });

        tick++;
    }
    while (!done);

    print_stats(std::cout, cmp, t_factories, mem_modules);

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