/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#ifndef MEMOPT_IMPL_VISZLAI_h
#define MEMOPT_IMPL_VISZLAI_h

#include "compiler/memopt/impl.h"

namespace memopt
{
namespace impl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    Named after the lead author of the corresponding paper
*/

class VISZLAI : public IMPL_BASE
{
public:
    using IMPL_BASE::inst_ptr;
    using IMPL_BASE::ws_type;
    using IMPL_BASE::inst_array;
    using IMPL_BASE::result_type;
    using IMPL_BASE::inst_window_type;
    using IMPL_BASE::inst_window_map;
public:
    VISZLAI(size_t cmp_count) : IMPL_BASE(cmp_count) {}

    result_type emit_memory_instructions(const ws_type& current_working_set, const inst_array& pending_inst, const inst_window_map& inst_windows) override;
private:
    void instruction_selection_iteration(inst_ptr, ws_type& new_working_set);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace impl
}  // namespace memopt

#endif