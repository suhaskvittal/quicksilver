/*
 *  author: Suhas Vittal
 *  date:   9 March 2026
 *
 *  Post-processing step that converts a binary to the RPC ISA.
 *  Run this after all compiler passes.
 * */

#include "argparse/argparse.h"
#include "compiler/program/rotation_manager.h"
#include "fixed_point/angle.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <cstdint>
#include <iostream>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string input_file;
    std::string output_file;
    int64_t     rdr_level;

    ARGPARSE()
        .required("input-file", "input binary file (non-RPC ISA)", input_file)
        .required("output-file", "output binary file (RPC ISA)", output_file)
        .optional("-rdr", "--rotation-recomputation-isa", "RPC level (default 1)", rdr_level, 1)
        .parse(argc, argv);

    compiler::prog::rotation_manager_init();

    generic_strm_type istrm, ostrm;
    generic_strm_open(istrm, input_file, "rb");
    generic_strm_open(ostrm, output_file, "wb");

    // copy the qubit-count header from input to output:
    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, sizeof(num_qubits));
    generic_strm_write(ostrm, &num_qubits, sizeof(num_qubits));

    size_t inst_count{0};
    while (true)
    {
        // read with RPC disabled so we don't try to read corr_urotseq data:
        GL_USE_RDR_ISA = 0;
        Instruction* inst = read_instruction_from_stream(istrm);
        if (inst == nullptr)
            break;

        // populate corrective sequences for rotation instructions:
        if (is_rotation_instruction(inst->type))
        {
            for (int64_t i = 0; i < rdr_level; i++)
            {
                inst->corr_urotseq_array.push_back(
                    compiler::prog::rotation_manager_lookup(fpa::scalar_mul(inst->angle, 2*(i+1))));
            }
        }

        // write with RPC enabled so corrective sequences are serialized:
        GL_USE_RDR_ISA = rdr_level;
        write_instruction_to_stream(ostrm, inst);

        delete inst;
        inst_count++;
    }

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    print_stat_line(std::cout, "INSTRUCTIONS_CONVERTED", inst_count);
    print_stat_line(std::cout, "RDR_LEVEL", rdr_level);

    compiler::prog::rotation_manager_end();
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
