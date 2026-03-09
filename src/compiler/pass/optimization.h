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

result_type cancel_and_coalesce(generic_strm_type& ostrm, generic_strm_type& istrm);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `run()` is a template function that generalizes the
 * common body observed in optimization passes.
 *
 * `INIT_CALLBACK` will be given a `IO_UTILITY` reference to initialize
 * any data with.
 *
 * `LOOP_CALLBACK` is called every loop iteration. The following is passed in:
 *      (1) the output `result_type`
 *      (2) the DAG
 *      (3) the IO_UTILITY
 *  If it returns true, then the loop is terminated early.
 *
 * `EXIT_CALLBACK` is called before the function exits to clean up any resources.
 * Like `INIT_CALLBACK`, `IO_UTILITY` is passed in case the pass needs to writeGkkjjkk
 * before exiting.
 *
 * Generally, the expectation is that the passes (declared above) will just
 * call `run` inside their body.
 * */
template <class INIT_CALLBACK, class LOOP_CALLBACK, class EXIT_CALLBACK>
result_type run(generic_strm_type& ostrm, 
                    generic_strm_type& istrm,
                    const INIT_CALLBACK&,
                    const LOOP_CALLBACK&,
                    const EXIT_CALLBACK&,
                    size_t dag_inst_capacity=8192);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace optimization
} // namespace pass
} // namespace compiler

#include "optimization.tpp"

#endif  // COMPILER_PASS_OPTIMIZATION_h
