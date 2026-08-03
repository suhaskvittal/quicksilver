/*
    author: Suhas Vittal
    date:   4 March 2026
*/

#include "argparse.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <algorithm>
#include <iostream>
#include <vector>

int main(int argc, char* argv[]) 
{
    std::string input_file;
    int64_t print_progress;

    ARGPARSE()
        .required("input-file", "Binary trace file to analyze", input_file)
        .optional("-rpc", "", "Use RPC ISA", GL_USE_RPC_ISA, 0)
        .optional("-pp", "--print-progress", "Progress print-out frequency", print_progress, 100'000)
        .parse(argc, argv);

    generic_strm_type istrm;
    generic_strm_open(istrm, input_file, "rb");

    uint32_t num_qubits_raw;
    generic_strm_read(istrm, &num_qubits_raw, sizeof(num_qubits_raw));
    size_t num_qubits = static_cast<size_t>(num_qubits_raw);

    // From CCZ_UOPS table in instruction.cpp:
    // CCZ: 4xT + 3xTDG = 7 T-like gates, 6xCX; CCX same 7 T-like + 6xCX
    constexpr uint64_t TOFFOLI_T_GATE_COUNT  = 7;
    constexpr uint64_t TOFFOLI_CX_GATE_COUNT = 6;

    uint64_t t_gates        = 0;
    uint64_t cx_cz_gates    = 0;
    uint64_t mem_accesses   = 0;
    uint64_t unrolled_insts = 0;

    // Instruction-level parallelism: ASAP-layer the compute DAG. `qubit_next_layer[q]`
    // is the earliest layer a future instruction on qubit `q` can occupy; a layer holds
    // mutually independent compute instructions. Software instructions are excluded (they
    // are not scheduled on hardware), so they neither occupy a layer nor advance depth.
    std::vector<int64_t> qubit_next_layer(num_qubits, 0);
    int64_t  num_layers         = 0;   // critical-path depth of the compute DAG
    uint64_t compute_inst_count = 0;   // non-software instructions

    uint64_t inst_count{0};
    while (!generic_strm_eof(istrm)) 
    {
        if (inst_count % print_progress == 0)
            std::cout << "progress: " << inst_count << " instructions read\n";

        INSTRUCTION* inst = read_instruction_from_stream(istrm);
        if (generic_strm_eof(istrm))
        {
            delete inst;
            break;
        }

        inst_count++;
        if (is_software_instruction(inst->type))
        {
            delete inst;
            continue;
        }

        // place this instruction in the earliest layer after all its operands are free,
        // then advance those operands past it. `num_layers` tracks the max depth reached.
        int64_t layer = 0;
        for (auto it = inst->q_begin(); it != inst->q_end(); ++it)
            layer = std::max(layer, qubit_next_layer[*it]);
        for (auto it = inst->q_begin(); it != inst->q_end(); ++it)
            qubit_next_layer[*it] = layer + 1;
        num_layers = std::max(num_layers, layer + 1);
        compute_inst_count++;

        size_t u = inst->unrolled_inst_count();
        if (is_rotation_instruction(inst->type))
            u = std::count_if(inst->urotseq.begin(), inst->urotseq.end(), [] (auto t) { return !is_software_instruction(t); });
        unrolled_insts += u;

        if (is_memory_access(inst->type))
        {
            mem_accesses++;
        }
        else if (is_t_like_instruction(inst->type))
        {
            t_gates++;
        }
        else if (is_rotation_instruction(inst->type))
        {
            t_gates += std::count_if(inst->urotseq.begin(), inst->urotseq.end(), [] (auto t) { return is_t_like_instruction(t); });
        }
        else if (is_cx_like_instruction(inst->type))
        {
             cx_cz_gates++;
        }
        else if (is_toffoli_like_instruction(inst->type)) 
        {
            t_gates      += TOFFOLI_T_GATE_COUNT;
            cx_cz_gates  += TOFFOLI_CX_GATE_COUNT;
        }
        else if (inst->type == INSTRUCTION::TYPE::H
                || is_s_like_instruction(inst->type))
        {
        }
        else
        {
            std::cout << "ignoring: " << *inst << _die{};
        }

        delete inst;
    }

    generic_strm_close(istrm);

    double t_fraction = mean(t_gates, unrolled_insts);
    double cx_fraction = mean(cx_cz_gates, unrolled_insts);
    double mem_fraction = mean(mem_accesses, unrolled_insts);

    print_stat_line(std::cout, "NUM_QUBITS",          num_qubits);
    print_stat_line(std::cout, "T_GATES",             t_gates);
    print_stat_line(std::cout, "CX_CZ_GATES",         cx_cz_gates);
    print_stat_line(std::cout, "MEMORY_ACCESSES",     mem_accesses);
    print_stat_line(std::cout, "UNROLLED_INST_COUNT", unrolled_insts);

    print_stat_line(std::cout, "T_GATE_%",     100*t_fraction);
    print_stat_line(std::cout, "CX_GATE_%",    100*cx_fraction);
    print_stat_line(std::cout, "MEM_%",        100*mem_fraction);

    // instruction-level parallelism = mean compute instructions per DAG layer
    print_stat_line(std::cout, "COMPUTE_INST_COUNT", compute_inst_count);
    print_stat_line(std::cout, "DAG_LAYERS",         num_layers);
    print_stat_line(std::cout, "MEAN_ILP",           mean(compute_inst_count, num_layers));
}
