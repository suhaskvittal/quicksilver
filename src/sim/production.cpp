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
                                size_t _buffer_capacity,
                                size_t _input_count,
                                size_t _output_count)
    :OPERABLE(name, freq_khz),
    output_error_probability(_output_error_probability),
    buffer_capacity(_buffer_capacity),
    input_count(_input_count),
    output_count(_output_count)
{
    if (output_count > buffer_capacity)
    {
        std::cerr << "in instantiation of PRODUCER_BASE " << name << ": buffer capacity cannot"
                    << " hold all output resource states." << _die{};
    }
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
    if (buffer_occupancy_ + output_count > buffer_capacity || production_step())
        return 1;
    else 
        return 0;
}

void
PRODUCER_BASE::install_resource_states()
{
    assert(buffer_occupancy_ + output_count <= buffer_capacity);
    buffer_occupancy_ += output_count;
}

size_t
PRODUCER_BASE::buffer_occupancy() const
{
    return buffer_occupancy_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim
