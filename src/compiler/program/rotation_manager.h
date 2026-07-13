/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#ifndef COMPILER_PROGRAM_ROTATION_MANAGER_h
#define COMPILER_PROGRAM_ROTATION_MANAGER_h

#include "instruction.h"

#include <vector>

namespace compiler
{
namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Initialize the rotation manager's lookup tables.
 * Must be called before any calls to `rotation_manager_lookup`.
 */
void rotation_manager_init();

/*
 * Cleans up rotation manager resources.
 * */
void rotation_manager_end();

/*
 * Searches for the urotseq for the given rotation
 * */
Instruction::urotseq_type rotation_manager_lookup(const Instruction::fpa_type&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace prog
}  // namespace compiler

#endif  // COMPILER_PROGRAM_ROTATION_MANAGER_h
