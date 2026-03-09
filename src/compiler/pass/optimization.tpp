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

template <class INIT_CALLBACK, class LOOP_CALLBACK, class EXIT_CALLBACK> result_type
run(generic_strm_type& ostrm, 
    generic_strm_type& istrm, 
    const INIT_CALLBACK& f_init, 
    const LOOP_CALLBACK& f_loop,
    const EXIT_CALLBACK& f_exit,
    size_t dag_inst_capacity)
{
    using dag_ptr = std::unique_ptr<DAG>;

    result_type out{};
    IO_UTILITY io(istrm, ostrm);
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
