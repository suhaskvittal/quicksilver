/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"

#include <unordered_map>
#include <vector>
#include <sstream>
#include <iomanip>

namespace sim
{

COMPUTE* GL_CMP;

std::mt19937 GL_RNG{0};
std::uniform_real_distribution<double> FP_RAND{0.0, 1.0};

uint64_t GL_CURRENT_TIME_NS{0};

// simulation wall clock start time
std::chrono::steady_clock::time_point GL_SIM_WALL_START;

bool GL_PRINT_PROGRESS{false};
int64_t GL_PRINT_PROGRESS_FREQ{-1};

bool GL_IMPL_RZ_PREFETCH{false};

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
        print_stat_line(out, "INST_ROUTING_STALL_CYCLES", c->s_inst_routing_stall_cycles);
        print_stat_line(out, "INST_RESOURCE_STALL_CYCLES", c->s_inst_resource_stall_cycles);
        print_stat_line(out, "INST_MEMORY_STALL_CYCLES", c->s_inst_memory_stall_cycles);
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

    // print memory stats:
    
    // accumulate any client-level stats:
    MEMORY_MODULE::client_stats_type num_pf{};
    MEMORY_MODULE::client_stats_type num_pf_promoted_to_demand{};
    for (MEMORY_MODULE* m : mem_modules)
    {
        for (int i = 0; i < clients.size(); i++)
        {
            num_pf[i] += m->s_num_prefetch_requests[i];
            num_pf_promoted_to_demand[i] += m->s_num_prefetch_promoted_to_demand[i];
        }
    }

    std::cout << "MEMORY\n";
    for (int i = 0; i < clients.size(); i++)
    {
        print_stat_line(out, "CLIENT_" + std::to_string(i) + "_NUM_PREFETCH_REQUESTS", num_pf[i]);
        print_stat_line(out, "CLIENT_" + std::to_string(i) + "_NUM_PREFETCH_PROMOTED_TO_DEMAND", num_pf_promoted_to_demand[i]);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
sim_walltime()
{
    auto duration = std::chrono::duration<double>(std::chrono::steady_clock::now() - GL_SIM_WALL_START);
    double total_seconds = duration.count();
    
    int minutes = static_cast<int>(total_seconds) / 60;
    double remaining_seconds = total_seconds - (minutes * 60);
    int seconds = static_cast<int>(remaining_seconds);
    int milliseconds = static_cast<int>((remaining_seconds - seconds) * 1000);
    
    std::ostringstream oss;
    oss << minutes << "m " << seconds << "s " << milliseconds << "ms";
    return oss.str();
}

double
sim_walltime_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - GL_SIM_WALL_START).count();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim