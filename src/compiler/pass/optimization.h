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

struct Result
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
    Result&
    operator+=(Result r)
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

Result cancel_and_coalesce(generic_strm_type& ostrm, generic_strm_type& istrm);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `run()` is a template function that generalizes the
 * common body observed in optimization passes.
 *
 * `InitCallback` will be given a `IOUtility` reference to initialize
 * any data with.
 *
 * `LoopCallback` is called every loop iteration. The following is passed in:
 *      (1) the output `Result`
 *      (2) the DAG
 *      (3) the IOUtility
 *  If it returns true, then the loop is terminated early.
 *
 * `ExitCallback` is called before the function exits to clean up any resources.
 * Like `InitCallback`, `IOUtility` is passed in case the pass needs to writeGkkjjkk
 * before exiting.
 *
 * Generally, the expectation is that the passes (declared above) will just
 * call `run` inside their body.
 * */
template <class InitCallback, class LoopCallback, class ExitCallback>
Result run(generic_strm_type& ostrm, 
                    generic_strm_type& istrm,
                    const InitCallback&,
                    const LoopCallback&,
                    const ExitCallback&,
                    size_t dag_inst_capacity=8192);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace optimization
} // namespace pass
} // namespace compiler

#include "optimization.tpp"

#endif  // COMPILER_PASS_OPTIMIZATION_h
