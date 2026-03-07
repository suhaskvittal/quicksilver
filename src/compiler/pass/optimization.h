/*
 *  author: Suhas Vittal
 *  date:   6 March 2026
 * */

#ifndef COMPILER_PASS_OPTIMIZATION_h
#define COMPILER_PASS_OPTIMIZATION_h

#include "generic_io.h"

#include <cstdint>

namespace compiler
{
namespace pass
{
namespace optimization
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct result_type
{
    /*
     * Amount of progress: definition of progress is up-to
     * the pass.
     *
     * If nonzero, all passes are rerun.
     * */
    long progress{0};

    /*
     * Add statistics for your optimization passes here:
     * */
    uint64_t s_gates_removed{0};

    /*
     * This is a utility operator for aggregating results
     * from multiple passes:
     * */
    result_type&
    operator+=(result_type r)
    {
        progress += r.progress;
        s_gates_removed += r.s_gates_removed;
        return *this;
    }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Pass declarations go here:
 * */

result_type gate_cancellation(generic_strm_type& ostrm, generic_strm_type& istrm);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace optimization
} // namespace pass
} // namespace compiler

#endif  // COMPILER_PASS_OPTIMIZATION_h
