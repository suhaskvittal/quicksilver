/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/operable.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

OPERABLE::OPERABLE(std::string_view _name, double _freq_khz)
    :name(_name),
    freq_khz(_freq_khz)
{}

void
OPERABLE::tick()
{
    if (leap_ < 1.0)
    {
        long progress = operate();
        if (progress == 0)
        {
            cycles_with_no_progress_++;
            if (cycles_with_no_progress_ >= GL_MAX_CYCLES_WITH_NO_PROGRESS)
            {
                std::cerr << name << ": deadlock --------------------------------------\n";
                print_deadlock_info(std::cerr);
                exit(1);
            }
        }
        else
        {
            cycles_with_no_progress_ = 0;
        }
        leap_ += clock_scale_;
        current_cycle_++;
    }
    else
    {
        leap_ -= 1.0;
    }
}

cycle_type
OPERABLE::current_cycle() const
{
    return current_cycle_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
compute_freq_khz(uint64_t p_ns)
{
    return 1e6 / static_cast<double>(p_ns);
}

cycle_type
convert_cycles_between_frequencies(cycle_type cycles, double original_freq_khz, double new_freq_khz)
{
    return static_cast<cycle_type>(std::ceil(cycles * new_freq_khz / original_freq_khz));
}

uint64_t
convert_cycles_to_time_ns(cycle_type c, double f)
{
    double time_s = c / (f * 1e3);
    return static_cast<uint64_t>(std::round(time_s * 1e9));
}

cycle_type
convert_time_ns_to_cycles(uint64_t t_ns, double f)
{
    return static_cast<cycle_type>(std::ceil((t_ns*1e-9) * (f*1e3)));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
coordinate_clock_scale(std::vector<OPERABLE*> operables)
{
    std::vector<double> freq_array(operables.size());
    std::transform(operables.begin(), operables.end(), freq_array.begin(), [] (auto* op) { return op->freq_khz; });
    double max_freq = *std::max_element(freq_array.begin(), freq_array.end());
    for (auto* op : operables)
        op->clock_scale_ = max_freq / op->freq_khz - 1.0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
fast_forward_all_operables_to_time_ns(std::vector<OPERABLE*> operables, uint64_t target_time_ns)
{
    for (auto* op : operables)
        op->current_cycle_ = convert_time_ns_to_cycles(target_time_ns, op->freq_khz);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
