/*
    author: Suhas Vittal
    date:   4 March 2026
*/

#include "argparse/argparse.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <algorithm>
#include <iostream>

int main(int argc, char* argv[]) 
{
    std::string input_file;
    int64_t print_progress;

    ARGPARSE()
        .required("input-file", "Binary trace file to analyze", input_file)
        .optional("-rdr", "", "Use RDR ISA", GL_USE_RDR_ISA, 0)
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

    uint64_t t_gates{0};
    uint64_t cx_cz_gates{0};
    uint64_t mem_accesses{0};
    uint64_t unrolled_insts{0};

    uint64_t t_gates_from_rz{0};
    uint64_t t_gates_from_ccx{0};

    uint64_t inst_count{0};
    while (!generic_strm_eof(istrm)) 
    {
        if (inst_count % print_progress == 0)
            std::cout << "progress: " << inst_count << " instructions read\n";

        INSTRUCTION* inst = read_instruction_from_stream(istrm);
        if (inst == nullptr)
            break;

        inst_count++;
        if (is_software_instruction(inst->type))
        {
            delete inst;
            continue;
        }
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
            const size_t n = std::count_if(inst->urotseq.begin(), inst->urotseq.end(), [] (auto t) { return is_t_like_instruction(t); });
            t_gates += n;
            t_gates_from_rz += n;
        }
        else if (is_cx_like_instruction(inst->type))
        {
             cx_cz_gates++;
        }
        else if (is_toffoli_like_instruction(inst->type)) 
        {
            t_gates      += TOFFOLI_T_GATE_COUNT;
            cx_cz_gates  += TOFFOLI_CX_GATE_COUNT;

            t_gates_from_ccx += TOFFOLI_T_GATE_COUNT;
        }
        else if (inst->type == INSTRUCTION::TYPE::H || is_s_like_instruction(inst->type))
        {
        }
        else
        {
            std::cout << "ignoring: " << *inst << _die{};
        }

        delete inst;
    }

    std::cout << "DONE\n";

    generic_strm_close(istrm);

    double t_fraction = mean(t_gates, unrolled_insts);
    double t_fraction_rz = mean(t_gates_from_rz, unrolled_insts);
    double t_fraction_ccx = mean(t_gates_from_ccx, unrolled_insts);
    double cx_fraction = mean(cx_cz_gates, unrolled_insts);
    double mem_fraction = mean(mem_accesses, unrolled_insts);

    print_stat_line(std::cout, "NUM_QUBITS",          num_qubits);
    print_stat_line(std::cout, "T_GATES",             t_gates);
    print_stat_line(std::cout, "T_GATES_FROM_RZ",     t_gates_from_rz);
    print_stat_line(std::cout, "T_GATES_FROM_CCX",    t_gates_from_ccx);
    print_stat_line(std::cout, "CX_CZ_GATES",         cx_cz_gates);
    print_stat_line(std::cout, "MEMORY_ACCESSES",     mem_accesses);
    print_stat_line(std::cout, "UNROLLED_INST_COUNT", unrolled_insts);

    print_stat_line(std::cout, "T_GATE_%",     100*t_fraction);
    print_stat_line(std::cout, "T_GATE_RZ_%",  100*t_fraction_rz);
    print_stat_line(std::cout, "T_GATE_CCX_%", 100*t_fraction_ccx);
    print_stat_line(std::cout, "CX_GATE_%",    100*cx_fraction);
    print_stat_line(std::cout, "MEM_%",        100*mem_fraction);
}
