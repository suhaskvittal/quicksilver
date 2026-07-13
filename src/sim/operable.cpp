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

extern int64_t GL_MAX_CYCLES_WITH_NO_PROGRESS;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Operable::Operable(std::string_view _name, double _freq_khz)
    :name(_name),
    freq_khz(_freq_khz)
{}

void
Operable::tick()
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

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

uint64_t
convert_cycles_to_time_ns(cycle_type c, double f)
{
    double time_s = c / (f * 1e3);
    return static_cast<uint64_t>(std::round(time_s * 1e9));
}

cycle_type
convert_time_ns_to_cycles(uint64_t t_ns, double f)
{
    // cycles = time_s * freq_hz = (t_ns * 1e-9) * (f * 1e3) = t_ns * f / 1e6.
    // Dividing by the exactly-representable 1e6 (instead of multiplying by the
    // inexact 1e-6) keeps a whole number of cycles exact, so ceil() does not
    // spuriously over-provision by one cycle for some inputs.
    return static_cast<cycle_type>(std::ceil(static_cast<double>(t_ns) * f / 1e6));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
coordinate_clock_scale(std::vector<Operable*> operables)
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
fast_forward_all_operables_to_time_ns(std::vector<Operable*> operables, uint64_t target_time_ns)
{
    for (auto* op : operables)
        op->current_cycle_ = convert_time_ns_to_cycles(target_time_ns, op->freq_khz);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
