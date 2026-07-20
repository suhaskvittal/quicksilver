/*
 *  author: Claude (Opus 4.8)
 *  date:   20 July 2026
 *
 *  Generates a synthetic benchmark of a single qubit executing `n` T gates,
 *  alternating between T and Tx (i.e. T, Tx, T, Tx, ...). Useful for isolating
 *  T-gate latency/throughput behavior in the reaction-time simulator.
 * */

#include "argparse/argparse.h"
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
    std::string output_file;
    int64_t     num_t_gates;

    ARGPARSE()
        .required("output-file", "output binary trace file", output_file)
        .required("num-t-gates", "number of T gates to emit", num_t_gates)
        .parse(argc, argv);

    generic_strm_type ostrm;
    generic_strm_open(ostrm, output_file, "wb");

    // single-qubit benchmark: write the qubit-count header:
    uint32_t num_qubits = 1;
    generic_strm_write(ostrm, &num_qubits, sizeof(num_qubits));

    // alternate T, Tx, T, Tx, ... all on qubit 0:
    for (int64_t i = 0; i < num_t_gates; i++)
    {
        Instruction::Type t = (i % 2 == 0) ? Instruction::Type::T : Instruction::Type::TX;
        Instruction inst(t, {0});
        write_instruction_to_stream(ostrm, &inst);
    }

    generic_strm_close(ostrm);

    print_stat_line(std::cout, "T_GATES_GENERATED", num_t_gates);
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
