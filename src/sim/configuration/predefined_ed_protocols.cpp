/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#include "sim/configuration/predefined_ed_protocols.h"
#include "sim/configuration/allocator/impl.h"

namespace sim
{
namespace configuration
{
namespace ed
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_0(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{4}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [3,1,3]_x
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=1e-2,
        .input_count=3,
        .output_count=1,
        .dx=3,
        .dz=1
    };

    ED_SPECIFICATION l2_spec   // [2,1,2]_y
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=1e-4,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=2
    };

    ED_SPECIFICATION l3_spec   // [2,1,2]_x
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=2e-8,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=1
    };

    ED_SPECIFICATION l4_spec   // [[6,4,2]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=3e-15,
        .input_count=6,
        .output_count=4,
        .dx=2,
        .dz=2
    };

    return {l1_spec, l2_spec, l3_spec, l4_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_1(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{18}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [[17,9,4]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=9,
        .output_error_rate=3e-6,
        .input_count=17,
        .output_count=9,
        .dx=4,
        .dz=4
    };

    ED_SPECIFICATION l2_spec   // [[25,18,3]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=3e-13,
        .input_count=25,
        .output_count=18,
        .dx=3,
        .dz=3
    };

    return {l1_spec, l2_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_2(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{14}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [[14,6,4]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=6,
        .output_error_rate=5e-6,
        .input_count=14,
        .output_count=6,
        .dx=4,
        .dz=4
    };

    ED_SPECIFICATION l2_spec   // [[16,14,2]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=1e-9,
        .input_count=16,
        .output_count=14,
        .dx=2,
        .dz=2
    };

    return {l1_spec, l2_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_3(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{5}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec   // [2,1,2]_y
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=7e-3,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=2
    };

    ED_SPECIFICATION l2_spec   // [2,1,2]_x
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=2e-4,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=1
    };

    ED_SPECIFICATION l3_spec   // [[11,5,3]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=2e-10,
        .input_count=11,
        .output_count=5,
        .dx=3,
        .dz=3
    };

    return {l1_spec,l2_spec,l3_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace ed
} // namespace configuration
} // namespace sim
