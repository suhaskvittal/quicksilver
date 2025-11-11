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

bool GL_ELIDE_MSWAP_INSTRUCTIONS{false};
bool GL_ELIDE_MPREFETCH_INSTRUCTIONS{false};

bool GL_IMPL_DECOUPLED_LOAD_STORE{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T, class U> double
_fpdiv(T a, U b)
{
    return static_cast<double>(a) / static_cast<double>(b);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats(std::ostream& out)
{
    std::vector<T_FACTORY*> t_factories = GL_CMP->get_t_factories();
    std::vector<MEMORY_MODULE*> mem_modules = GL_CMP->get_mem_modules();
    
    // print stats for each client:
    out << "\n\nSIMULATION_STATS------------------------------------------------------------\n";
    double execution_time_us = (GL_CMP->current_cycle() / GL_CMP->OP_freq_khz) * 1e3;
    double execution_time_s = execution_time_us / 1e6;

    const auto& clients = GL_CMP->get_clients();
    for (size_t i = 0; i < clients.size(); i++)
    {
        const auto& c = clients[i];
        out << "CLIENT_" << i << "\n";
        double kilo_inst_per_s = (c->s_unrolled_inst_done / execution_time_s) * 1e-3;

//      print_stat_line(out, "TIME_PER_UNROLLED_INST (us/inst)", execution_time_us / c->s_unrolled_inst_done);
        print_stat_line(out, "KIPS", kilo_inst_per_s);
        print_stat_line(out, "VIRTUAL_INST_DONE", c->s_inst_done);
        print_stat_line(out, "UNROLLED_INST_DONE", c->s_unrolled_inst_done);
//      print_stat_line(out, "INST_ROUTING_STALL_CYCLES", c->s_inst_routing_stall_cycles);
//      print_stat_line(out, "INST_RESOURCE_STALL_CYCLES", c->s_inst_resource_stall_cycles);
//      print_stat_line(out, "INST_MEMORY_STALL_CYCLES", c->s_inst_memory_stall_cycles);
        print_stat_line(out, "MEMORY_SWAPS", c->s_mswap_count);
        print_stat_line(out, "MEMORY_PREFETCHES", c->s_mprefetch_count);
        print_stat_line(out, "T_GATE_COUNT", c->s_t_gate_count);
        print_stat_line(out, "T_TOTAL_ERROR", c->s_total_t_error);
    }

    print_stat_line(out, "EVICTIONS_NO_USES", GL_CMP->s_evictions_no_uses, false);
    print_stat_line(out, "EVICTIONS_PREFETCH_NO_USES", GL_CMP->s_evictions_prefetch_no_uses, false);
    print_stat_line(out, "OPERATIONS_WITH_DECOUPLED_LOADS", GL_CMP->s_operations_with_decoupled_loads, false);

    // print factory stats:
    std::unordered_map<size_t, std::vector<T_FACTORY*>> factory_level_map;
    for (auto* f : t_factories)
        factory_level_map[f->level_].push_back(f);

    for (size_t i = 0; i < factory_level_map.size(); i++)
    {
        out << "FACTORY_L" << i << "\n";

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
    uint64_t total_memory_requests = std::transform_reduce(mem_modules.begin(), mem_modules.end(), uint64_t{0}, std::plus<uint64_t>(),
                                                    [] (auto* m) { return m->s_memory_requests; });
    uint64_t total_memory_prefetch_requests = std::transform_reduce(mem_modules.begin(), mem_modules.end(), uint64_t{0}, std::plus<uint64_t>(),
                                                    [] (auto* m) { return m->s_memory_prefetch_requests; });
    uint64_t total_epr_buffer_occupancy_post_request = std::transform_reduce(mem_modules.begin(), mem_modules.end(), uint64_t{0}, std::plus<uint64_t>(),
                                                    [] (auto* m) { return m->s_total_epr_buffer_occupancy_post_request; });
    double mean_epr_buffer_occupancy_post_request = _fpdiv(total_epr_buffer_occupancy_post_request, total_memory_requests);

    uint64_t total_decoupled_loads = std::transform_reduce(mem_modules.begin(), mem_modules.end(), uint64_t{0}, std::plus<uint64_t>(),
                                                    [] (auto* m) { return m->s_decoupled_loads; });
    uint64_t total_decoupled_stores = std::transform_reduce(mem_modules.begin(), mem_modules.end(), uint64_t{0}, std::plus<uint64_t>(),
                                                    [] (auto* m) { return m->s_decoupled_stores; });

    out << "MEMORY\n";
    print_stat_line(out, "ALL_REQUESTS", total_memory_requests);
    print_stat_line(out, "PREFETCH_REQUESTS", total_memory_prefetch_requests);
    print_stat_line(out, "MEAN_EPR_BUFFER_OCCUPANCY_POST_REQUEST", mean_epr_buffer_occupancy_post_request);
    print_stat_line(out, "DECOUPLED_LOADS", total_decoupled_loads);
    print_stat_line(out, "DECOUPLED_STORES", total_decoupled_stores);
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
