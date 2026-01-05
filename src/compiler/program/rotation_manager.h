/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#ifndef COMPILER_PROGRAM_ROTATION_MANAGER_h
#define COMPILER_PROGRAM_ROTATION_MANAGER_h

#include "instruction.h"

#include <vector>

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Initialize the rotation manager with worker threads.
 * Must be called before any synthesis operations.
 */
void rotation_manager_init(size_t num_threads=8);

/*
 * Shutdown the rotation manager and cleanup resources.
 * Should be called at program exit.
 */
void rotation_manager_end();

/*
 * Schedule a rotation for synthesis in the background.
 * Non-blocking - synthesis happens asynchronously.
 */
void rotation_manager_schedule_synthesis(const INSTRUCTION::fpa_type&, ssize_t precision);

/*
 * Retrieve the synthesized rotation sequence.
 * Blocks until synthesis is complete if not already done.
 * Returns the optimized gate sequence implementing the rotation.
 */
std::vector<INSTRUCTION::TYPE> rotation_manager_find(const INSTRUCTION::fpa_type&, ssize_t precision);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace prog

#endif  // COMPILER_PROGRAM_ROTATION_MANAGER_h
