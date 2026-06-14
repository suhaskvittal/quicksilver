/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"
#include "sim/driver.h"
#include "sim/production.h"
#include "sim/stats.h"

#include <iomanip>
#include <sstream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::chrono::steady_clock::time_point GL_SIM_WALL_START;
std::mt19937_64 GL_RNG{0};

int64_t GL_PRINT_PROGRESS_FREQUENCY{1'000'000};
int64_t GL_MAX_CYCLES_WITH_NO_PROGRESS{1'000'000};

double GL_PHYSICAL_ERROR_RATE{1e-3};

bool GL_T_GATE_DO_AUTOCORRECT{false};

bool GL_OPERATE_AS_NEUTRAL_ATOM{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int64_t GL_REACTION_TIME{10};
int64_t GL_RLTP_DEGREE{0};

bool GL_RDR_ENABLED{false};
int64_t GL_RDR_CAPACITY{2};
int64_t GL_RDR_START_LAYER{2};
int64_t GL_RDR_LOOKAHEAD_DEPTH{8};
int64_t GL_RDR_INST_DELTA_LIMIT{500};
int64_t GL_RDR_DEGREE{4};
int64_t GL_RDR_COMPLETION_BUFFER_CAPACITY{0};
bool GL_RDR_FIXED_LOOKAHEAD{false};
double GL_RDR_COST_SCALE{2.0};

bool GL_ELIDE_CLIFFORDS{false};
bool GL_ZERO_LATENCY_T_GATES{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Utility function for printing stats for each client.
 * */
void _print_client_stats(std::ostream&, DRIVER*, CLIENT*);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
walltime()
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
walltime_s()
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - GL_SIM_WALL_START).count();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_sim_stats(std::ostream& out, DRIVER* d)
{
    using STALL_TYPE = DRIVER::STALL_TYPE;

    uint64_t cx_gates{0};
    uint64_t t_gates{0};
    for (auto t : {INSTRUCTION::TYPE::CX, INSTRUCTION::TYPE::CZ})
        cx_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];
    for (auto t : {INSTRUCTION::TYPE::T, INSTRUCTION::TYPE::TX, INSTRUCTION::TYPE::TDG, INSTRUCTION::TYPE::TXDG})
        t_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];

    double t_consumption_rate_per_s = mean(t_gates, d->current_cycle() / (1e3*d->freq_khz));
    print_stat_line(out, "TOTAL_SIMULATION_CYCLES", d->current_cycle());

    print_stat_line(out, "CX_GATES_EXECUTED", cx_gates);
    print_stat_line(out, "T_GATES_EXECUTED", t_gates);
    print_stat_line(out, "T_CONSUMPTION_RATE_PER_S", t_consumption_rate_per_s);

    print_stat_line(out, "ISOLATED_MEMORY_STALLS", d->stall_monitor().isolated_stalls_for(STALL_TYPE::MEMORY));
    print_stat_line(out, "ISOLATED_MAGIC_STATE_STALLS", d->stall_monitor().isolated_stalls_for(STALL_TYPE::MAGIC_STATE));
    print_stat_line(out, "ISOLATED_EPR_STALLS", d->stall_monitor().isolated_stalls_for(STALL_TYPE::EPR));
    print_stat_line(out, "TOTAL_STALLS", d->stall_monitor().cycles_with_stalls());

    if (GL_RDR_ENABLED)
    {
        print_stat_line(out, "TOTAL_ROTATION_INSTRUCTIONS", d->s_rotation_instructions);
        print_stat_line(out, "RDR_REQUESTS_SUBMITTED", d->rdr()->s_requests_submitted);
        print_stat_line(out, "RDR_REQUESTS_INV_BEFORE_ISSUE", d->rdr()->s_requests_invalidated_before_issue);
        print_stat_line(out, "RDR_REQUESTS_STARTED", d->rdr()->s_requests_started);
        print_stat_line(out, "RDR_REQUESTS_COMPLETED", d->rdr()->s_requests_completed);
        print_stat_line(out, "RDR_REQUESTS_INTERRUPTED", d->rdr()->s_requests_interrupted);
        print_stat_line(out, "RDR_REQUESTS_INVALIDATED", d->rdr()->s_requests_invalidated);
        print_stat_line(out, "RDR_REQUESTS_USED", d->rdr()->s_requests_used);

        double completion_buffer_mean_occu = mean(d->rdr()->s_completion_buffer_occu_sum,
                                                   d->rdr()->s_completion_buffer_occu_ticks);
        print_stat_line(out, "RDR_COMPLETION_BUFFER_MEAN_OCCUPANCY", completion_buffer_mean_occu);

        double request_mean_latency = mean(d->rdr()->s_request_completion_cycles_sum, d->rdr()->s_requests_used);
        double request_mean_latency_per_uop = mean(d->rdr()->s_request_completion_cycles_sum, d->rdr()->s_request_uop_sum);
        print_stat_line(out, "RDR_REQUEST_COMPLETION_MEAN_LATENCY", request_mean_latency);
        print_stat_line(out, "RDR_MEAN_LATENCY_PER_UOP", request_mean_latency_per_uop);

        double request_idle_time = mean(d->rdr()->s_post_completion_idle_time_sum, d->rdr()->s_requests_used);
        print_stat_line(out, "RDR_REQUEST_POST_COMPLETION_MEAN_IDLE_TIME", request_idle_time);
    }

    for (auto* c : d->clients())
        _print_client_stats(out, d, c);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats_for_factories(std::ostream& out, std::string_view header, std::vector<PRODUCER_BASE*> factories)
{
    if (factories.empty())
        return;

    out << header << "\n";
    
    // accumulate stats:
    double freq_khz = factories[0]->freq_khz;
    uint64_t total_attempts{0},
             total_failures{0},
             total_consumed{0};
    for (auto* f : factories)
    {
        total_attempts += f->s_production_attempts;
        total_failures += f->s_failures;
        total_consumed += f->s_consumed;
    }
    double kill_rate = mean(total_failures, total_attempts);

    print_stat_line(std::cout, "    FACTORY_FREQ_KHZ", freq_khz);
    print_stat_line(std::cout, "    FACTORY_COUNT", factories.size());
    print_stat_line(std::cout, "    PRODUCED", total_attempts - total_failures);
    print_stat_line(std::cout, "    CONSUMED", total_consumed);
    print_stat_line(std::cout, "    KILL_RATE", kill_rate);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/* Helper functions start here */

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_print_client_stats(std::ostream& out, DRIVER* d, CLIENT* c)
{
    double ipc = stats::ipc(c->s_unrolled_inst_done, c->s_cycle_complete);
    double ipdc = stats::ipdc(c->s_unrolled_inst_done, c->s_cycle_complete, d->compute_subsystem()->code_distance);
    double kips = stats::kips(c->s_unrolled_inst_done, c->s_cycle_complete, d->freq_khz);

    double uops_per_rotation = mean(c->s_total_rotation_uops, c->s_total_rotations);
    double rotation_latency_per_uop = mean(c->s_rotation_latency, c->s_total_rotation_uops);
    double mean_memory_access_latency = mean(c->s_memory_access_latency, c->s_memory_accesses);

    out << "CLIENT " << static_cast<int>(c->id) << "\n";
    print_stat_line(out, "    IPC", ipc);
    print_stat_line(out, "    IPdC", ipdc);
    print_stat_line(out, "    KIPS", kips);
    print_stat_line(out, "    INSTRUCTIONS", c->s_unrolled_inst_done);
    print_stat_line(out, "    CYCLES", c->s_cycle_complete);
    print_stat_line(out, "    ROTATION_LATENCY_PER_UOP", rotation_latency_per_uop);
    print_stat_line(out, "    MEAN_UOPS_PER_ROTATION", uops_per_rotation);
    print_stat_line(out, "    MEMORY_ACCESSES", c->s_memory_accesses);
    print_stat_line(out, "    MEAN_MEMORY_ACCESS_LATENCY", mean_memory_access_latency);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
