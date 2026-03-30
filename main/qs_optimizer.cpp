/*
 *  author: Suhas Vittal
 *  date:   7 March 2026
 * */

#include "argparse/argparse.h"
#include "compiler/pass/optimization.h"
#include "compiler/program/rotation_manager.h"
#include "generic_io.h"
#include "instruction.h"

#include <chrono>
#include <cstdio>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using result_type = compiler::pass::optimization::result_type;

/*
 * This function does the following:
 *  (1) A tmp file is created
 *  (2) The input pass is called on the input stream and the tmp file.
 *      The result is used to update `running_result`
 *  (3) The input stream is closed.
 *  (4) The input stream is now updated to be the tmp file stream.
 * */
template <class P>
void run_pass(generic_strm_type&, const P&, result_type& running_result);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string input_file;
    std::string output_file;

    ARGPARSE()
        .required("input-file", "input binary file", input_file)
        .required("output-file", "output binary file", output_file)
        .parse(argc, argv);

    generic_strm_type istrm;
    // initially, `istrm` will point to the input file.
    generic_strm_open(istrm, input_file, "r");

    compiler::prog::rotation_manager_init();
    
    result_type total{};
    result_type out;
    size_t iter_idx{0};
    do
    {
        std::cout << "iteration " << iter_idx << "\n";

        // start timing this iteration:
        auto iter_start = std::chrono::high_resolution_clock::now();

        out = result_type{};
        /* passes start here */
        run_pass(istrm, compiler::pass::optimization::cancel_and_coalesce, out);

        // end of iteration -- return time it took to complete
        auto iter_end =  std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(iter_end - iter_start);
        std::cout << "\n\t\tt = " << duration.count() << "ms, gates removed = " << out.s_gates_removed << "\n";

        total += out;
        iter_idx++;
    }
    while (out.progress > 0);

    // finally, copy all the data from `istrm` to the output file:
    generic_strm_type ostrm;
    generic_strm_open(ostrm, output_file, "w");
    char copy_buf[4096];
    while (!generic_strm_eof(istrm))
    {
        size_t bytes_read = generic_strm_read(istrm, copy_buf, 4096);
        generic_strm_write(ostrm, copy_buf, bytes_read);
    }
    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    print_stat_line(std::cout, "GATES_KILLED", total.s_gates_removed);

    compiler::prog::rotation_manager_end();

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

template <class P> void
run_pass(generic_strm_type& istrm, const P& p, result_type& running_result)
{
    // create tmp file and call pass `p`
    generic_strm_type tmp_strm = tmpfile();
    running_result += p(tmp_strm, istrm);

    // set `istrm` to new tmp file stream, and move to beginning of file
    generic_strm_close(istrm);
    istrm = std::move(tmp_strm);
    generic_strm_seek(istrm, 0, SEEK_SET);
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
