/*
 *  author: Suhas Vittal
 *  date:   6 January 2026
 * */

#ifndef SIM_COMPUTE_CORE_h
#define SIM_COMPUTE_CORE_h

#include "globals.h"

#include <vector>

namespace sim
{
namespace compute
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Fetches all ready instructions from client and tries
 * to execute them. An instruction is considered ready
 * if its qubits are present in the provided range
 * from `q_begin` to `q_end`, and all qubits are
 * available at `current_cycle`
 *
 * On a successful execution, these instructions are retired. 
 * If an instruction has uops, then a uop is advanced.
 *
 * Returns the number of successfully executed instructions.
 * */
template <class COMPUTE_MODEL_PTR, class ITER>
size_t fetch_and_execute_instruction_from_client(COMPUTE_MODEL_PTR&, client_ptr&, ITER q_begin, ITER q_end);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace compute
}  // namespace sim

#endif // SIM_COMPUTE_CORE_h
