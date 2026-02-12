/*
 *  author: Suhas Vittal
 *  date:   11 February 2026
 * */

#ifndef COMPILER_PROGRAM_ROTATION_SYNTHESIS_h
#define COMPILER_PROGRAM_ROTATION_SYNTHESIS_h

#include "instruction.h"

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Computes the unrolled rotation sequence for the given angle (`fpa_type`).
 *
 * This rotation sequence is optimized using TACO (H-gate elision and gate coalescing).
 * */
INSTRUCTION::urotseq_type synthesize_rotation(const INSTRUCTION::fpa_type&, ssize_t precision, bool verbose);

/*
 * Checks if the given urotseq implements the given rotation up-to the given precision.
 * Return true if the urotseq is ok, false otherwise.
 *
 * If the urotseq is incorrect, then debug information is printed out to stderr.
 * */
bool validate_urotseq(const INSTRUCTION::urotseq_type&, const INSTRUCTION::fpa_type&, ssize_t precision);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog

#endif  // COMPILER_PROGRAM_ROTATION_SYNTHESIS_h
