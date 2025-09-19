/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"

#include <unordered_map>
#include <vector>

namespace sim
{

COMPUTE* GL_CMP;

std::mt19937 GL_RNG{0};
std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

bool GL_PRINT_PROGRESS{true};
uint64_t GL_PRINT_PROGRESS_FREQ{100000};
uint64_t GL_CURRENT_TIME_NS{0};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats(std::ostream& out)
{
    std::vector<T_FACTORY*> t_factories = GL_CMP->get_t_factories();
    std::vector<MEMORY_MODULE*> mem_modules = GL_CMP->get_mem_modules();
    
    // print stats for each client:
    out << "\n\nSIMULATION_STATS------------------------------------------------------------\n";
    double execution_time = (GL_CMP->current_cycle() / GL_CMP->OP_freq_khz) * 1e-3 / 60.0;

    print_stat_line(out, "TOTAL_CYCLES", GL_CMP->current_cycle(), false);
    print_stat_line(out, "COMPUTE_SPEED (kHz)", GL_CMP->OP_freq_khz, false);
    print_stat_line(out, "EXECUTION_TIME (min)", execution_time, false);
    
    const auto& clients = GL_CMP->get_clients();
    for (size_t i = 0; i < clients.size(); i++)
    {
        const auto& c = clients[i];
        std::cout << "CLIENT_" << i << "\n";

        print_stat_line(out, "VIRTUAL_INST_DONE", c->s_inst_done);
        print_stat_line(out, "UNROLLED_INST_DONE", c->s_unrolled_inst_done);
    }

    // print factory stats:
    std::unordered_map<size_t, std::vector<T_FACTORY*>> factory_level_map;
    for (auto* f : t_factories)
        factory_level_map[f->level_].push_back(f);

    for (size_t i = 0; i < factory_level_map.size(); i++)
    {
        std::cout << "FACTORY_L" << i << "\n";

        uint64_t tot_prod_tries{0},
                 tot_failures{0};
        for (auto* f : factory_level_map[i])
        {
            tot_prod_tries += f->s_prod_tries_;
            tot_failures += f->s_failures_;
        }

        print_stat_line(out, "PROD_TRIES", tot_prod_tries);
        print_stat_line(out, "FAILURES", tot_failures);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim