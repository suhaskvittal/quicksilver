/*
 *  contributor: OpenAI Codex (GPT-6.1 Sol), TopoLS integration, 1 October 2026
    author: Suhas Vittal
    date:   8 September 2025
    contributor: OpenAI Codex (TopoLS model removal, 30 September 2026)
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

bool GL_TOPOLS_ENABLED{false};
int64_t GL_TOPOLS_ANCILLA{4};

bool GL_ELIDE_CLIFFORDS{false};
bool GL_ZERO_LATENCY_T_GATES{false};
bool GL_MEMORY_IDEAL_RESOURCES{false};

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
    using StallType = Driver::StallType;

    uint64_t cx_gates{0};
    uint64_t t_gates{0};
    for (auto t : {Instruction::Type::CX, Instruction::Type::CZ})
        cx_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];
    for (auto t : {Instruction::Type::T, Instruction::Type::TX, Instruction::Type::TDG, Instruction::Type::TXDG})
        t_gates += d->compute_subsystem()->s_inst_executed_by_type[static_cast<int>(t)];

    double t_consumption_rate_per_s = fpdiv(t_gates, d->current_cycle()) * (1e3*d->freq_khz);
    print_stat_line(out, "TOTAL_SIMULATION_CYCLES", d->current_cycle());

    print_stat_line(out, "CX_GATES_EXECUTED", cx_gates);
    print_stat_line(out, "T_GATES_EXECUTED", t_gates);
    print_stat_line(out, "T_CONSUMPTION_RATE_PER_S", t_consumption_rate_per_s);

    print_stat_line(out, "ISOLATED_MEMORY_STALLS", d->stall_monitor().isolated_stalls_for(StallType::MEMORY));
    print_stat_line(out, "ISOLATED_MAGIC_STATE_STALLS", d->stall_monitor().isolated_stalls_for(StallType::MAGIC_STATE));
    print_stat_line(out, "ISOLATED_EPR_STALLS", d->stall_monitor().isolated_stalls_for(StallType::EPR));
    print_stat_line(out, "TOTAL_STALLS", d->stall_monitor().cycles_with_stalls());

    auto m = d->compute_subsystem()->memory_subsystem();
    for (size_t i = 0; i < m.size(); i++)
    {
        out << "MEMORY_SUBSYSTEM_L" << (i+1) << "\n";
        print_stat_line(out, "    NAME", m[i]->name);
        print_stat_line(out, "    FREQ_KHZ", m[i]->freq_khz);
        print_stat_line(out, "    CYCLES", m[i]->current_cycle());
        print_stat_line(out, "    SURGERY_OPERATIONS", m[i]->s_surgery_operations);
        print_stat_line(out, "    SHIFT_AUTOMOPRHISMS", m[i]->s_shift_automorphisms);
    }
    
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

    if (GL_TOPOLS_ENABLED)
    {
        out << "TOPOLS\n";
        auto* t = d->topols();
        print_stat_line(out, "    REQUESTS_ALLOCATED", t->s_requests_allocated);
        print_stat_line(out, "    REQUESTS_COMPLETED", t->s_requests_completed);
        print_stat_line(out, "    REQUESTS_FULL_LATENCY_HIDDEN", t->s_requests_full_latency_hidden);
        t->s_latency.dump(out, 1);
        t->s_hidden_gates.dump(out, 1);
        t->s_hidden_latency.dump(out, 1);
        t->s_fraction_hidden_latency.dump(out, 1);
    }
    for (auto* c : d->clients())
        _print_client_stats(out, d, c);
    print_stats_for_factories(out, "T_FACTORY", d->compute_subsystem()->t_producer());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
print_stats_for_factories(std::ostream& out, std::string_view header, const BulkProducer* producer)
{
    if (producer == nullptr || producer->level_count() == 0)
        return;

    out << header << "\n";

    print_stat_line(std::cout, "    FACTORY_FREQ_KHZ", producer->freq_khz);
    print_stat_line(std::cout, "    OVERALL_INFIDELITY", producer->top_level_infidelity());
    for (size_t i = 0; i < producer->level_count(); i++)
    {
        const auto& lvl = producer->level(i);
        std::string prefix = "    L" + std::to_string(i) + "_";
        print_stat_line(std::cout, prefix + "PRODUCER_COUNT", lvl.producer_count);
        print_stat_line(std::cout, prefix + "COMPLETED", lvl.s_completions);
        print_stat_line(std::cout, prefix + "CYCLES_NO_INPUT", lvl.s_cycles_stalled_on_input);
        print_stat_line(std::cout, prefix + "CYCLES_BUFFER_FULL", lvl.s_cycles_stalled_on_buffer);
    }
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
