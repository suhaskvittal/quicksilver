/*
 *  author: Suhas Vittal
 *  date:   2 July 2026
 * */

#ifndef COMPILER_PASS_INSTRUCTION_SCHEDULER_h
#define COMPILER_PASS_INSTRUCTION_SCHEDULER_h

namespace compiler
{
namespace pass
{
namespace scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Interaction graph for instruction schedulers.
 * */
class InteractionGraph
{
public:
    using node_type = Instruction;
    struct edge_type
    {
        node_type* u{};
        node_type* v{};
    };

    struct node_data
    {
         
    };
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SchedulerImpl>
Stats run(generic_strm_type& ostrm, generic_strm_type& istrm, const SchedulerImpl&, Config);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace scheduler
} // namespace pass
} // namespace compiler

#endif 
