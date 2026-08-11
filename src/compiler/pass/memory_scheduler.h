/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef COMPILER_PASS_MEMORY_SCHEDULER_h
#define COMPILER_PASS_MEMORY_SCHEDULER_h

#include "dag.h"
#include "generic_io.h"
#include "compiler/pass/util.h"
#include "stats.h"

#include <memory>
#include <unordered_set>

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Type aliases for common types used throughout memory scheduler
 * */

using inst_ptr = DAG::inst_ptr;
using dag_ptr = std::unique_ptr<DAG>;
using active_set_type = std::unordered_set<qubit_type>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `Config` allows the user to control
 * execution knobs, such as verbosity or active set
 * size.
 * */

struct Config
{
    int64_t active_set_capacity{12};
    int64_t inst_compile_limit{15'000'000};
    int64_t print_progress_frequency{1'000'000};
    int64_t dag_inst_capacity{8192};
    bool    verbose{false};

    /* EIF specific parameters */
    int64_t eif_lookahead_depth{0};

    /* HINT specific parameters */
    int64_t hint_lookahead_depth{16};
    bool    hint_use_complex_selection{true};
    bool    hint_use_nonarbitrary_victim_selection{false};

    /* Other */
    bool count_whole_instructions{false};
};

/*
 * `Stats` contains relevant compilation
 * statistics. Feel free to add your own.
 * */

struct Stats
{
    uint64_t unrolled_inst_done{0};
    uint64_t memory_accesses{0};
    uint64_t scheduler_epochs{0};
    stats::Histogram<uint64_t> unused_bandwidth{"UNUSED_BANDWIDTH", 0, 32, 8};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * For `run` (see below), the user must pass in
 * a function (or lambda) that corresponds to their
 * memory scheduler implementation (essentially, what happens
 * when no more compute instructions can be scheduled
 * on some set of active qubits).
 *
 * This function must take in:
 *  (1) the current active set of qubits (i.e., `const active_set_type&`)
 *  (2) a reference to the DAG (i.e., `const dag_ptr&`)
 *  (3) the configuration (`Config`)
 *
 * And return `Result`, which is defined here.
 *
 * For constructing `Result`, it is recommended to create
 * a "target active set" (essentially, what you want to have
 * in the active set) and call `transform_active_set` which
 * will handle memory instruction generation for you.
 * */

struct Result
{
    /*
     * This is the list of load/store instructions generated during
     * this scheduling epoch.
     * */
    std::vector<inst_ptr> memory_accesses;

    /*
     * This is the updated `active_set`.
     * */
    active_set_type active_set;

    /*
     * Number of qubits left untouched by the memory accesses.
     * A nonzero number indicates that other memory accesses
     * could've been done (they may not be useful, however).
     * */
    size_t unused_bandwidth;
};

/*
 * Transforms the current active set to match the target active set
 * by generating load/store instructions.
 *
 * Typically, a memory scheduler will identify *what* it wants
 * in the active set. This function converts between the
 * current active set and desired active set.
 *
 * The user can also supply an array of scores. Qubits with higher
 * scores are prioritized for eviction.
 * */
result_type transform_active_set(const active_set_type& current,
                                 const active_set_type& target,
                                 std::vector<double> scores);

/*
 * Returns true if all of the instruction's args are in `active_set`
 * */
bool instruction_is_ready(inst_ptr, const active_set_type&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * This is the main scheduler function.
 *
 * `schedule` schedules memory accesses for
 * the instructions contained in the file pointed to by
 * `istrm` and writes the new program to `ostrm`.
 *
 * The user must provide a scheduler implementation that
 * has an `emit_memory_instructions` method with signature:
 *   Result emit_memory_instructions(const active_set_type&,
 *                                        const dag_ptr&,
 *                                        Config)
 * */
template <class SchedulerImpl>
Stats run(generic_strm_type& ostrm, generic_strm_type& istrm, const SchedulerImpl&, Config);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler

#include "compiler/pass/memory_scheduler.tpp"

#endif  // COMPILER_PASS_MEMORY_SCHEDULER_h
