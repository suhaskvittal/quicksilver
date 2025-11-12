/*
    author: Suhas Vittal
    date:   30 September 2025
*/

#ifndef COMPILER_MEMOPT_IMPL_COST_AWARE_h
#define COMPILER_MEMOPT_IMPL_COST_AWARE_h

#include "compiler/memopt/impl.h"

namespace memopt
{
namespace impl
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// implementation is an inverted tree
struct WORKING_SET_TREE_NODE
{
    std::unordered_set<qubit_type> working_set;
    // want `compute_value` to be as high as possible and `memory_cost` to be as low as possible
    int32_t compute_value{0};
    int32_t memory_cost{0};
    bool is_frozen{false};

    WORKING_SET_TREE_NODE* child{nullptr};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COST_AWARE : public IMPL_BASE
{
public:
    using IMPL_BASE::inst_ptr;
    using IMPL_BASE::ws_type;
    using IMPL_BASE::inst_array;
    using IMPL_BASE::result_type;
    using IMPL_BASE::inst_window_type;
    using IMPL_BASE::inst_window_map;

    const bool use_simple_version;
private:
    double tot_score{0.0};
    double num_scores{0.0};
public:
    COST_AWARE(size_t cmp_count, bool _use_simple_version=false) 
        : IMPL_BASE(cmp_count), use_simple_version(_use_simple_version) 
    {}

    result_type emit_memory_instructions(const ws_type& current_working_set, const inst_array& pending_inst, 
                                        const inst_window_map& inst_windows) override;
private:
    // working set, compute intensity, qubit scores
    using working_set_search_result_type = std::tuple<ws_type, double, std::vector<double>>;

    void update_dp_tree(std::vector<WORKING_SET_TREE_NODE*>& entry_points, inst_ptr inst);
    working_set_search_result_type compute_best_working_set(const std::vector<WORKING_SET_TREE_NODE*>& entry_points);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int64_t get_compute_value_of_instruction(INSTRUCTION::TYPE);
WORKING_SET_TREE_NODE* working_set_tree_traverse(WORKING_SET_TREE_NODE* start);
void working_set_tree_free(std::vector<WORKING_SET_TREE_NODE*>&& entry_points);

double score_working_set(int64_t compute_value, int64_t memory_cost);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace impl
}   // namespace memopt

#endif
