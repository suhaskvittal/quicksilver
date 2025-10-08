/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#ifndef SIM_UTILS_FACTORY_BUILDER_h
#define SIM_UTILS_FACTORY_BUILDER_h

#include "sim/factory.h"

namespace sim
{
namespace util
{

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

struct FACTORY_INFO
{
    std::string which;
    size_t      sc_dx;
    size_t      sc_dz;
    size_t      sc_dm;
    double      e_out;
};

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

using factory_build_result_type = std::pair<std::vector<T_FACTORY*>, size_t>;

std::vector<FACTORY_INFO> make_factory_config(double target_error_rate)
T_FACTORY*                create_factory_from_info(const FACTORY_INFO&, 
                                                        double freq_khz, 
                                                        size_t level,
                                                        size_t buffer_capacity=4);

// returns the factory vector and the actual qubits used
factory_build_result_type factory_build(double target_error_rate, 
                                            size_t max_phys_qubits, 
                                            uint64_t t_round_ns,
                                            size_t pin_limit=4);

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

}   // namespace util
}   // namespace sim

#endif  // SIM_UTILS_FACTORY_BUILDER_h
