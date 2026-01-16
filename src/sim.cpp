/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"

#include <iomanip>
#include <sstream>

namespace sim
{

std::chrono::steady_clock::time_point GL_SIM_WALL_START;
std::mt19937_64 GL_RNG{0};

int64_t GL_PRINT_PROGRESS_FREQUENCY{1'000'000};
int64_t GL_MAX_CYCLES_WITH_NO_PROGRESS{5000};

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
print_client_stats(std::ostream& out, COMPUTE_SUBSYSTEM* compute_subsystem, CLIENT* c)
{
    double ipc = c->ipc();

    // kilo-instructions per second may be useful in some scenarios:
    double kips = mean(c->s_unrolled_inst_done / 1000, c->s_cycle_complete / (compute_subsystem->freq_khz*1e3));

    out << "CLIENT " << static_cast<int>(c->id) << "\n";
    print_stat_line(out, "    IPC", ipc);
    print_stat_line(out, "    KIPS", kips);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
