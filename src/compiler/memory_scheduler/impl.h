/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef COMPILER_MEMORY_SCHEDULER_IMPL_h
#define COMPILER_MEMORY_SCHEDULER_IMPL_h

#include "compiler/memory_scheduler.h"

namespace compile
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * EIF (Earliest Instructions First) scheduler policy.
 * */
result_type eif(const active_set_type&, const dag_ptr&, config_type);

/*
 * HINT (High Intensity) scheduler policy.
 * */
result_type hint(const active_set_type&, const dag_ptr&, config_type);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace memory_scheduler
}   // namespace compile

#endif  // COMPILER_MEMORY_SCHEDULER_IMPL_h
