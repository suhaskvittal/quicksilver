/*
    author: Suhas Vittal
    date:   8 September 2025
*/

#include "sim.h"
#include "sim/factory.h"

#include <iomanip>
#include <sstream>

namespace sim
{

std::chrono::steady_clock::time_point GL_SIM_WALL_START;
std::mt19937_64 GL_RNG{0};

cycle_type GL_MAX_CYCLES_WITH_NO_PROGRESS{5000};

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
print_stats_for_factories(std::ostream& out, std::string_view header, std::vector<T_FACTORY_BASE*> factories)
{
    out << header << "\n";
    
    // accumulate stats:
    double freq_khz = factories[0]->freq_khz;
    uint64_t total_attempts = std::transform_reduce(factories.begin(), factories.end(), uint64_t{0},
                                                    std::plus<uint64_t>{},
                                                    [] (const auto* f) { return f->s_production_attempts; });
    uint64_t total_failures = std::transform_reduce(factories.begin(), factories.end(), uint64_t{0},
                                                    std::plus<uint64_t>{},
                                                    [] (const auto* f) { return f->s_failures; });
    double kill_rate = mean(total_failures, total_attempts);

    print_stat_line(std::cout, "    FACTORY_FREQ_KHZ", freq_khz);
    print_stat_line(std::cout, "    FACTORY_COUNT", factories.size());
    print_stat_line(std::cout, "    KILL_RATE", kill_rate);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim
