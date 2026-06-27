/*
 *  author: Suhas Vittal
 *  date:   14 January 2026
 * */

#ifndef SIM_CONFIGURATION_RESOURCE_ESTIMATION_h
#define SIM_CONFIGURATION_RESOURCE_ESTIMATION_h

#include <cstddef>

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Returns `2*d*(d+1)` (we use this to account for any slack qubits surrounding the patch)
 * */
constexpr size_t surface_code_physical_qubit_count(size_t d);
constexpr size_t surface_code_physical_qubit_count(size_t dx, size_t dz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * These return the physical qubit and logical qubit counts for the sub-family of BB codes
 * whose parameters follow [[12*d, 12, d]] (i.e., [[72, 12, 6]], or [[144, 12, 12]])
 * */
constexpr size_t bivariate_bicycle_code_physical_qubit_count(size_t d);
constexpr size_t bivariate_bicycle_code_logical_qubit_count(size_t d);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Resource estimates for magic state factories:
 *
 * Routing overheads for distillation are assumed to be `(input_count+output+count)/2`
 * */
constexpr size_t magic_state_cultivation_physical_qubit_count(size_t escape_distance);
constexpr size_t magic_state_distillation_physical_qubit_count(size_t input_count, 
                                                                size_t output_count,
                                                                size_t dx, 
                                                                size_t dz);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Logical error rate estimates for different codes.
 * Cannot be `constexpr` since these use floats.
 * */
double surface_code_logical_error_rate(size_t, double p);
double bivariate_bicycle_code_block_error_rate(size_t, double p);

size_t surface_code_distance_for_target_logical_error_rate(double, double p);
size_t bivariate_bicycle_code_distance_for_target_block_error_rate(double, double p);

/*
 * This is for a code-concatenation scheme:
 * */
size_t inner_surface_code_distance_for_target_logical_error_rate(double, size_t outer_d, double p);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace configuration
}  // namespace sim

#include "resource_estimation.tpp"

#endif  // SIM_CONFIGURATION_RESOURCE_ESTIMATION_h
