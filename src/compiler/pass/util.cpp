/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include "compiler/pass/util.h"

namespace compiler
{
namespace pass
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
read_instructions_into_dag(std::unique_ptr<DAG>& dag, generic_strm_type& istrm, size_t until_capacity)
{
    while (dag->inst_count() < until_capacity && !generic_strm_eof(istrm))
    {
        DAG::inst_ptr inst = read_instruction_from_stream(istrm);
        dag->add_instruction(inst);
    }
}

bool
instruction_is_ready(DAG::inst_ptr inst, const std::unordered_set<qubit_type>& active_set)
{
    return is_software_instruction(inst->type)
           || std::all_of(inst->q_begin(), inst->q_end(), [&active_set] (auto q) { return active_set.count(q) > 0; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace pass
}  // namespace compiler
