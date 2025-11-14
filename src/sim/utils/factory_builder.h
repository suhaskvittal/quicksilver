/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#ifndef SIM_UTILS_FACTORY_BUILDER_h
#define SIM_UTILS_FACTORY_BUILDER_h

#include "sim/factory.h"

#include <tuple>
#include <vector>

namespace sim
{

extern bool GL_PREF_CULTIVATION;

namespace util
{

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

struct FACTORY_INFO
{
    std::string which;
    double      e_out;

    // only used by distillation:
    size_t      sc_dx;
    size_t      sc_dz;
    size_t      sc_dm;

    // only used by cultivation
    double      probability_of_success;
    size_t      cult_d;
    size_t      num_rounds;

    bool is_cultivation() const { return which == "c3" || which == "c5"; }

    constexpr static FACTORY_INFO 
    distillation(std::string which, double e_out, size_t sc_dx, size_t sc_dz, size_t sc_dm)
    {
        return {.which=which, .e_out=e_out, .sc_dx=sc_dx, .sc_dz=sc_dz, .sc_dm=sc_dm};
    }

    constexpr static FACTORY_INFO 
    cultivation(std::string which, double e_out, double probability_of_success, size_t cult_d, size_t num_rounds)
    {
        return {.which=which, .e_out=e_out, .probability_of_success=probability_of_success, .cult_d=cult_d, .num_rounds=num_rounds};
    }
};

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

using factory_build_result_type = std::tuple<std::vector<T_FACTORY*>, size_t, std::vector<FACTORY_INFO>>;

std::vector<FACTORY_INFO> make_factory_config(double target_error_rate);
T_FACTORY*                create_factory_from_info(const FACTORY_INFO&, 
                                                        uint64_t round_ns,
                                                        size_t level,
                                                        size_t buffer_capacity=16);

size_t get_factory_qubit_count(const FACTORY_INFO& fi);

// returns the factory vector and the actual qubits used
factory_build_result_type factory_build(double target_error_rate, 
                                            size_t max_phys_qubits, 
                                            uint64_t l1_sc_round_ns,
                                            uint64_t l2_sc_round_ns,
                                            size_t pin_limit=4);

////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////

}   // namespace util
}   // namespace sim

#endif  // SIM_UTILS_FACTORY_BUILDER_h
