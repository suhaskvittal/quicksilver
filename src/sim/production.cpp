/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#include "sim/production.h"

#include <cassert>
#include <iomanip>
#include <iostream>
#include <random>
#include <sstream>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PRODUCER_BASE::PRODUCER_BASE(std::string_view name,
                                double freq_khz,
                                double _output_error_probability,
                                size_t _buffer_capacity)
    :OPERABLE(name, freq_khz),
    output_error_probability(_output_error_probability),
    buffer_capacity(_buffer_capacity)
{
}

void
PRODUCER_BASE::consume(size_t count)
{
    assert(count <= buffer_occupancy_);
    buffer_occupancy_ -= count;
    s_consumed += count;
}

void
PRODUCER_BASE::print_deadlock_info(std::ostream& out) const
{
    out << name << ": buffer occupancy = " << buffer_occupancy_ << " of " << buffer_capacity << "\n";
}

long
PRODUCER_BASE::operate()
{
    if (buffer_occupancy_ >= buffer_capacity || production_step())
        return 1;
    else 
        return 0;
}

void
PRODUCER_BASE::install_resource_state()
{
    assert(buffer_occupancy_ < buffer_capacity);
    buffer_occupancy_++;
}

size_t
PRODUCER_BASE::buffer_occupancy() const
{
    return buffer_occupancy_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
