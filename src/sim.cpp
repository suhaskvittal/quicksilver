/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"

#include <unordered_map>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats(std::ostream& out, COMPUTE* cmp, std::vector<T_FACTORY*> t_factories, std::vector<MEMORY_MODULE*> mem_modules)
{
    // print stats for each client:
    out << "\n\nSIMULATION_STATS------------------------------------------------------------\n";
    double execution_time = (cmp->current_cycle() / cmp->freq_khz_) * 1e-3 / 60.0;

    print_stat_line(out, "TOTAL_CYCLES", cmp->current_cycle(), false);
    print_stat_line(out, "COMPUTE_SPEED (kHz)", cmp->freq_khz_, false);
    print_stat_line(out, "EXECUTION_TIME (min)", execution_time, false);
    
    const auto& clients = cmp->clients();
    for (size_t i = 0; i < clients.size(); i++)
    {
        const auto& c = clients[i];
        std::cout << "CLIENT_" << i << "\n";

        print_stat_line(out, "VIRTUAL_INST_DONE", c->s_inst_done);
        print_stat_line(out, "UNROLLED_INST_DONE", c->s_unrolled_inst_done);

        print_stat_line(out, "CYCLES_STALLED", c->s_cycles_stalled);
        print_stat_line(out, "CYCLES_STALLED_BY_MEM_ONLY", c->s_cycles_stalled_by_type[COMPUTE::EXEC_RESULT_MEMORY_STALL]);
        print_stat_line(out, "CYCLES_STALLED_BY_ROUTING_ONLY", c->s_cycles_stalled_by_type[COMPUTE::EXEC_RESULT_ROUTING_STALL]);
        print_stat_line(out, "CYCLES_STALLED_BY_RESOURCE_ONLY", c->s_cycles_stalled_by_type[COMPUTE::EXEC_RESULT_RESOURCE_STALL]);
        print_stat_line(out, "CYCLES_STALLED_BY_WAITING_FOR_QUBIT_TO_BE_READY", c->s_cycles_stalled_by_type[COMPUTE::EXEC_RESULT_WAITING_FOR_QUBIT_TO_BE_READY]);

        print_stat_line(out, "CYCLES_STALLED_BY_MEM_AND_RESOURCE_ONLY", 
                    c->s_cycles_stalled_by_type[COMPUTE::EXEC_RESULT_MEMORY_STALL | COMPUTE::EXEC_RESULT_RESOURCE_STALL]);
    }

    // print factory stats:
    std::unordered_map<size_t, std::vector<T_FACTORY*>> factory_level_map;
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

        print_stat_line(out, "PROD_TRIES", tot_prod_tries);
        print_stat_line(out, "FAILURES", tot_failures);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_progress(std::ostream& out, COMPUTE* cmp, uint64_t tick, uint64_t dot_freq, uint64_t newline_freq)
{
    if (tick % dot_freq == 0)
    {
        if (tick % newline_freq == 0)
        {
                out << "\nCMP CYCLE @ " << cmp->current_cycle() << " [";
                for (const auto& c : cmp->clients())
                {
                    uint64_t inst_done_trunc = c->s_inst_done / 1000;
                    std::string postfix{"K"};
                    if (inst_done_trunc > 1000)
                    {
                        inst_done_trunc /= 1000;
                        postfix = "M";
                    }
                    out << std::setw(4) << std::right << inst_done_trunc << postfix;
                }
                out << " ]\t";
        }
        out << ".";
        out.flush();
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim