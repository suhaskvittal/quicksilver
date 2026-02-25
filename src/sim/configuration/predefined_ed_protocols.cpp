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
    ll_buffer_capacity = std::max(int64_t{2}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [2,1,2]_x
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=8.4e-3,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=1
    };

    ED_SPECIFICATION l2_spec   // [2,1,2]_y
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=1.4e-4,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=2
    };

    ED_SPECIFICATION l3_spec   // [2,1,2]_x
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .output_error_rate=1.2e-6,
        .input_count=2,
        .output_count=1,
        .dx=2,
        .dz=1
    };

    ED_SPECIFICATION l4_spec   // [[4,2,2]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=4.5e-12,
        .input_count=4,
        .output_count=2,
        .dx=2,
        .dz=2
    };

    return {l1_spec, l2_spec, l3_spec, l4_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_3(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{18}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [[4,2,2]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=3,
        .output_error_rate=4.9e-4,
        .input_count=4,
        .output_count=2,
        .dx=2,
        .dz=2
    };

    ED_SPECIFICATION l2_spec   // [[27,18,4]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=4.3e-10,
        .input_count=27,
        .output_count=18,
        .dx=4,
        .dz=4
    };

    return {l1_spec, l2_spec};

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_4(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{23}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [[8,3,3]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=3,
        .output_error_rate=3.85e-5,
        .input_count=8,
        .output_count=3,
        .dx=3,
        .dz=3
    };

    ED_SPECIFICATION l2_spec   // [[30,23,3]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=9.05e-11,
        .input_count=30,
        .output_count=23,
        .dx=3,
        .dz=3
    };

    return {l1_spec, l2_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
protocol_5(int64_t c_round_time_ns, int64_t ll_buffer_capacity)
{
    ll_buffer_capacity = std::max(int64_t{25}, ll_buffer_capacity);

    ED_SPECIFICATION l1_spec  // [[14,6,4]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=6,
        .output_error_rate=4.4e-6,
        .input_count=14,
        .output_count=6,
        .dx=4,
        .dz=4
    };

    ED_SPECIFICATION l2_spec // [[32,25,3]]
    {
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=6.1e-13,
        .input_count=32,
        .output_count=25,
        .dx=3,
        .dz=3
    };

    return {l1_spec, l2_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace ed
} // namespace configuration
} // namespace sim
