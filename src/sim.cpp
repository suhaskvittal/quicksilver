/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"
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
int64_t GL_T_GATE_TELEPORTATION_MAX{0};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool GL_RPC_ALWAYS_USE_TELEPORTATION{false};
bool GL_RPC_ALWAYS_RUNAHEAD{false};
int64_t GL_RPC_INST_DELTA_LIMIT{500};
int64_t GL_RPC_DEGREE{4};

bool GL_ELIDE_CLIFFORDS{false};
bool GL_ZERO_LATENCY_T_GATES{false};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Utility function for printing stats for each client.
 * */
void _print_client_stats(std::ostream&, COMPUTE_SUBSYSTEM*, CLIENT*);

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
print_compute_subsystem_stats(std::ostream& out, COMPUTE_SUBSYSTEM* compute_subsystem)
{
    using STALL_TYPE = COMPUTE_SUBSYSTEM::STALL_TYPE;

    const auto* rotation_subsystem = compute_subsystem->rotation_subsystem();

    uint64_t total_t_consumption{compute_subsystem->s_t_gates};
    uint64_t total_t_teleports{compute_subsystem->s_t_gate_teleports};
    uint64_t total_t_gate_episodes{compute_subsystem->s_t_gate_teleport_episodes};

    if (rotation_subsystem != nullptr)
    {
        total_t_consumption += rotation_subsystem->s_t_gates;
        total_t_teleports += rotation_subsystem->s_t_gate_teleports;
        total_t_gate_episodes += rotation_subsystem->s_t_gate_teleport_episodes;
    }

    double t_consumption_rate = mean(total_t_consumption, compute_subsystem->current_cycle());
    double t_consumption_rate_per_s = mean(total_t_consumption, compute_subsystem->current_cycle() / (1e3*compute_subsystem->freq_khz));
    double t_teleports_per_episode = mean(total_t_teleports, total_t_gate_episodes);

    print_stat_line(out, "COMPUTE_FREQ_KHZ", compute_subsystem->freq_khz);
    print_stat_line(out, "TOTAL_SIMULATION_CYCLES", compute_subsystem->current_cycle());

    print_stat_line(out, "T_GATES_EXECUTED", total_t_consumption);
    print_stat_line(out, "T_GATE_TELEPORTATIONS", total_t_teleports);
    print_stat_line(out, "T_GATE_TELEPORTATIONS_PER_EPISODE", t_teleports_per_episode);
    print_stat_line(out, "T_CONSUMPTION_RATE_PER_CYCLE", t_consumption_rate);
    print_stat_line(out, "T_CONSUMPTION_RATE_PER_S", t_consumption_rate_per_s);

    print_stat_line(out, "TOTAL_ROTATIONS", compute_subsystem->s_total_rotations);
    print_stat_line(out, "RPC_TOTAL", compute_subsystem->s_total_rpc);
    print_stat_line(out, "RPC_SUCCESSFUL", compute_subsystem->s_successful_rpc);
    print_stat_line(out, "CYCLES_WITH_RPC_STALLS", compute_subsystem->s_cycles_with_rpc_stalls);

    print_stat_line(out, "ISOLATED_MEMORY_STALLS", compute_subsystem->stall_monitor().isolated_stalls_for(STALL_TYPE::MEMORY));
    print_stat_line(out, "ISOLATED_RESOURCE_STALLS", compute_subsystem->stall_monitor().isolated_stalls_for(STALL_TYPE::RESOURCE));
    print_stat_line(out, "TOTAL_STALLS", compute_subsystem->stall_monitor().cycles_with_stalls());

    if (rotation_subsystem != nullptr)
    {
        double mean_rpc_cycle_latency = mean(rotation_subsystem->s_rotation_service_cycles, 
                                            rotation_subsystem->s_rotations_completed),
               mean_rpc_idle_cycles = mean(rotation_subsystem->s_rotation_idle_cycles,
                                            rotation_subsystem->s_rotations_completed);
        print_stat_line(out, "RPC_SERVICE_CYCLES", mean_rpc_cycle_latency);
        print_stat_line(out, "RPC_IDLE_CYCLES", mean_rpc_idle_cycles);
        print_stat_line(out, "RPC_INVALIDATES", rotation_subsystem->s_invalidates);
    }

    for (auto* c : compute_subsystem->clients())
        _print_client_stats(out, compute_subsystem, c);
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
_print_client_stats(std::ostream& out, COMPUTE_SUBSYSTEM* compute_subsystem, CLIENT* c)
{
    double ipc = stats::ipc(c->s_unrolled_inst_done, c->s_cycle_complete);
    double ipdc = stats::ipdc(c->s_unrolled_inst_done, c->s_cycle_complete, compute_subsystem->code_distance);
    double kips = stats::kips(c->s_unrolled_inst_done, c->s_cycle_complete, compute_subsystem->freq_khz);

    double rotation_latency_per_uop = mean(c->s_rotation_latency, c->s_total_rotation_uops);
    double mean_memory_access_latency = mean(c->s_memory_access_latency, c->s_memory_accesses);

    out << "CLIENT " << static_cast<int>(c->id) << "\n";
    print_stat_line(out, "    IPC", ipc);
    print_stat_line(out, "    IPdC", ipdc);
    print_stat_line(out, "    KIPS", kips);
    print_stat_line(out, "    INSTRUCTIONS", c->s_unrolled_inst_done);
    print_stat_line(out, "    CYCLES", c->s_cycle_complete);
    print_stat_line(out, "    ROTATION_LATENCY_PER_UOP", rotation_latency_per_uop);
    print_stat_line(out, "    MEMORY_ACCESSES", c->s_memory_accesses);
    print_stat_line(out, "    MEAN_MEMORY_ACCESS_LATENCY", mean_memory_access_latency);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
