/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

namespace compiler
{
namespace pass
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER> void
drain_buffer_into_stream(ITER begin, ITER end, generic_strm_type& ostrm)
{
    std::for_each(begin, end,
            [&ostrm] (DAG::inst_ptr inst)
            {
                write_instruction_to_stream(ostrm, inst);
                delete inst;
            });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace pass
}  // namespace compiler
