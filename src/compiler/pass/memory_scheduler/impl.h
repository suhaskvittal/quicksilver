/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef COMPILER_PASS_MEMORY_SCHEDULER_IMPL_h
#define COMPILER_PASS_MEMORY_SCHEDULER_IMPL_h

#include "compiler/pass/memory_scheduler.h"

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * EIF (Earliest Instructions First) scheduler policy.
 * */
Result eif(const active_set_type&, const dag_ptr&, Config);

/*
 * HINT (High Intensity) scheduler policy.
 * */
Result hint(const active_set_type&, const dag_ptr&, Config);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace memory_scheduler
}   // namespace pass
}   // namespace compiler

#endif  // COMPILER_PASS_MEMORY_SCHEDULER_IMPL_h
