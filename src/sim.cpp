/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"
#include "sim/driver.h"
#include "sim/production.h"
#include "sim/metrics.h"

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
double GL_RDR_INV_THRESHOLD{0.33};

bool GL_ELIDE_CLIFFORDS{false};
bool GL_ZERO_LATENCY_T_GATES{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Utility function for printing stats for each client.
 * */
void _print_client_stats(std::ostream&, Driver*, Client*);

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
print_sim_stats(std::ostream& out, Driver* d)
{
    using Stall = Driver::Stall;

    uint64_t cx_gates{0};
    uint64_t t_gates{0};
    for (auto t : {Instruction::Type::CX, Instruction::Type::CZ})
        cx_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];
    for (auto t : {Instruction::Type::T, Instruction::Type::TX, Instruction::Type::TDG, Instruction::Type::TXDG})
        t_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];

    double t_consumption_rate_per_s = fpdiv(t_gates, d->current_cycle() / (1e3*d->freq_khz));
    print_stat_line(out, "TOTAL_SIMULATION_CYCLES", d->current_cycle());

    print_stat_line(out, "CX_GATES_EXECUTED", cx_gates);
    print_stat_line(out, "T_GATES_EXECUTED", t_gates);
    print_stat_line(out, "T_CONSUMPTION_RATE_PER_S", t_consumption_rate_per_s);

    print_stat_line(out, "ISOLATED_MEMORY_STALLS", d->stall_monitor().isolated_stalls_for(Stall::MEMORY));
    print_stat_line(out, "ISOLATED_MAGIC_STATE_STALLS", d->stall_monitor().isolated_stalls_for(Stall::MAGIC_STATE));
    print_stat_line(out, "ISOLATED_EPR_STALLS", d->stall_monitor().isolated_stalls_for(Stall::EPR));
    print_stat_line(out, "TOTAL_STALLS", d->stall_monitor().cycles_with_stalls());

    if (GL_RDR_ENABLED)
    {
        out << "RDR\n";
        print_stat_line(out, "    TOTAL_ROTATION_INSTRUCTIONS", d->s_rotation_instructions);
        print_stat_line(out, "    REQUESTS_SUBMITTED", d->rdr()->s_requests_submitted);
        print_stat_line(out, "    REQUESTS_INV_BEFORE_ISSUE", d->rdr()->s_requests_invalidated_before_issue);
        print_stat_line(out, "    REQUESTS_STARTED", d->rdr()->s_requests_started);
        print_stat_line(out, "    REQUESTS_COMPLETED", d->rdr()->s_requests_completed);
        print_stat_line(out, "    REQUESTS_INTERRUPTED", d->rdr()->s_requests_interrupted);
        print_stat_line(out, "    REQUESTS_INVALIDATED", d->rdr()->s_requests_invalidated);
        print_stat_line(out, "    REQUESTS_USED", d->rdr()->s_requests_used);

        d->rdr()->s_request_latency.dump(out, 1);
        d->rdr()->s_request_latency_norm_uop.dump(out, 1);
        d->rdr()->s_post_completion_idle_time.dump(out, 1);
        d->rdr()->s_completion_buffer_occu.dump(out, 1);
    }

    for (auto* c : d->clients())
        _print_client_stats(out, d, c);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats_for_factories(std::ostream& out, std::string_view header, std::vector<ProducerBase*> factories)
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
    double kill_rate = fpdiv(total_failures, total_attempts);

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
_print_client_stats(std::ostream& out, Driver* d, Client* c)
{
    double ipc = stats::ipc(c->s_unrolled_inst_done, c->s_cycle_complete),
           ipdc = stats::ipdc(c->s_unrolled_inst_done, c->s_cycle_complete, d->compute_subsystem()->code_distance),
           kips = stats::kips(c->s_unrolled_inst_done, c->s_cycle_complete, d->freq_khz);

    out << "CLIENT " << static_cast<int>(c->id) << "\n";
    print_stat_line(out, "    IPC", ipc);
    print_stat_line(out, "    IPdC", ipdc);
    print_stat_line(out, "    KIPS", kips);
    print_stat_line(out, "    INSTRUCTIONS", c->s_unrolled_inst_done);
    print_stat_line(out, "    CYCLES", c->s_cycle_complete);
    // histograms
    c->s_rotation_latency.dump(out, 1);
    c->s_rotation_uops.dump(out, 1);
    c->s_memory_access_latency.dump(out, 1);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
