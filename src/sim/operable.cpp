/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#include "sim/operable.h"

#include <algorithm>
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

void
coordinate_clock_scale(std::vector<OPERABLE*> operables)
{
    std::vector<double> freq_array(operables.size());
    std::transform(operables.begin(), operables.end(), freq_array.begin(), [] (auto* op) { return op->freq_khz; });
    double max_freq = *std::max_element(freq_array.begin(), freq_array.end());
    for (auto* op : operables)
        op->clock_scale_ = max_freq / op->freq_khz;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
