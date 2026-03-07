/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef COMPILER_PASS_UTIL_h
#define COMPILER_PASS_UTIL_h

#include "dag.h"
#include "generic_io.h"

#include <memory>
#include <unordered_set>

namespace compiler
{
namespace pass
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Reads instructions into the DAG until `DAG::inst_count() >= until_capacity`
 * */
void read_instructions_into_dag(std::unique_ptr<DAG>&, generic_strm_type&, size_t until_capacity);

/*
 * Returns true if all of the instruction's args are in `active_set`
 * */
bool instruction_is_ready(DAG::inst_ptr, const std::unordered_set<qubit_type>&);

/*
 * Drains all instructions from `begin` to `end` and writes them to `ostrm`.
 * Instructions are also freed after doing so.
 * */
template <class ITER>
void drain_buffer_into_stream(ITER begin, ITER end, generic_strm_type& ostrm);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace pass
}  // namespace compiler

#include "compiler/pass/util.tpp"

#endif  // COMPILER_PASS_UTIL_h
