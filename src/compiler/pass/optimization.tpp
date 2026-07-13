/*
 *  author: Suhas Vittal
 *  date:   9 March 2026
 * */

#include "dag.h"
#include "compiler/pass/util.h"

#include <memory>

namespace compiler
{
namespace pass
{
namespace optimization
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class InitCallback, class LoopCallback, class ExitCallback> Result
run(generic_strm_type& ostrm, 
    generic_strm_type& istrm, 
    const InitCallback& f_init, 
    const LoopCallback& f_loop,
    const ExitCallback& f_exit,
    size_t dag_inst_capacity)
{
    using dag_ptr = std::unique_ptr<DAG>;

    Result out{};
    IOUtility io(istrm, ostrm);
    dag_ptr dag{new DAG{io.num_qubits}};

    f_init(io);

    while (dag->inst_count() > 0 || !generic_strm_eof(istrm))
    {
        io.read_instructions(dag.get(), dag_inst_capacity);
        if (f_loop(out, dag.get(), io))
            break;
    }

    f_exit(io);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace optimization
}  // namespace pass
}  // namespace compiler
