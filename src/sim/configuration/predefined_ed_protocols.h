/*
 *  author: Suhas Vittal
 *  date:   24 February 2026
 * */

#ifndef SIM_CONFIGURATION_PREDEFINED_ED_PROTOCOLS_h
#define SIM_CONFIGURATION_PREDEFINED_ED_PROTOCOLS_h

#include <cstdint>
#include <vector>

namespace sim
{
namespace configuration
{

struct ED_SPECIFICATION;  // forward-declaration (see `sim/configuration/allocator/impl.h`)
                          
namespace ed
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * There isn't really any rhyme or reason as to the number of these protocols. They
 * are numbered in order of when they were coded in.
 *
 * `c_round_time_ns` -- syndrome extraction round time in nanoseconds
 * `ll_buffer_capacity` -- buffer capacity of last level
 *
 * See: https://dspace.mit.edu/bitstream/handle/1721.1/162579/3695053.3731069.pdf?sequence=1&isAllowed=y
 *
 * 0 = [3,1,3]_x * [2,1,2]_y * [2,1,2]_x * [[6,4,2]]  -- output error is 1e-15
 * 1 = [[17,9,4]] * [[25,18,3]]                       -- output error is 3e-13      
 *
 * 2 = [2,1,2]_x * [2,1,2]_y * [2,1,2]_x * [[4,2,2]]  -- output error is 4.5e-12    * found using code
 * 3 = [[17,9,4]] * [[25,18,3]]                        -- output error is 7.4e-11    * found using code
 * */
std::vector<ED_SPECIFICATION> protocol_0(int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION> protocol_1(int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION> protocol_2(int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION> protocol_3(int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION> protocol_4(int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION> protocol_5(int64_t ll_buffer_capacity);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace ed
} // namespace configuration
} // namespace sim

#endif // SIM_CONFIGURATION_PREDEFINED_ED_PROTOCOLS_h
