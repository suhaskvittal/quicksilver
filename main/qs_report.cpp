/*
    author: Suhas Vittal
    date:   10 November 2025
*/

#include "argparse.h"
#include "generic_io.h"
#include "instruction.h"

#include <iostream>
#include <iomanip>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
print_stat_line(std::ostream& out, std::string name, T value)
{
    out << std::setw(64) << std::left << name;
    if constexpr (std::is_floating_point<T>::value)
        out << std::setw(12) << std::right << std::fixed << std::setprecision(8) << value;
    else
        out << std::setw(12) << std::right << value;
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool is_software_instruction(INSTRUCTION::TYPE type)
{
    return type == INSTRUCTION::TYPE::X
        || type == INSTRUCTION::TYPE::Y
        || type == INSTRUCTION::TYPE::Z
        || type == INSTRUCTION::TYPE::SWAP;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct ProgramStats
{
    uint32_t num_qubits{0};
    uint64_t total_instructions{0};
    uint64_t unrolled_instructions{0};
    uint64_t unrolled_t_gates{0};
    uint64_t s_gates{0};
    uint64_t h_gates{0};
    uint64_t cx_gates{0};
    uint64_t mswap_instructions{0};
    uint64_t mprefetch_instructions{0};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ProgramStats analyze_binary_file(const std::string& input_file)
{
    ProgramStats stats;
    
    generic_strm_type istrm;
    generic_strm_open(istrm, input_file, "rb");
    
    // Read number of qubits (first 4 bytes)
    generic_strm_read(istrm, &stats.num_qubits, sizeof(stats.num_qubits));
    
    std::cout << "[ QS_REPORT ] Reading binary file: " << input_file << std::endl;
    std::cout << "[ QS_REPORT ] Number of qubits: " << stats.num_qubits << std::endl;
    
    // Read and analyze each instruction
    while (!generic_strm_eof(istrm))
    {
        INSTRUCTION::io_encoding enc;

        // Try to read the instruction
        enc.read_write([&istrm] (void* buf, size_t size) {
            return generic_strm_read(istrm, buf, size);
        });

        // If we hit EOF during reading, break
        if (generic_strm_eof(istrm))
            break;
            
        // Create instruction from encoding
        INSTRUCTION inst(std::move(enc));
        stats.total_instructions++;

        // Count different gate types
        switch (inst.type)
        {
            case INSTRUCTION::TYPE::T:
            case INSTRUCTION::TYPE::TDG:
            case INSTRUCTION::TYPE::TX:
            case INSTRUCTION::TYPE::TXDG:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.unrolled_t_gates++;
                break;

            case INSTRUCTION::TYPE::S:
            case INSTRUCTION::TYPE::SDG:
            case INSTRUCTION::TYPE::SX:
            case INSTRUCTION::TYPE::SXDG:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.s_gates++;
                break;

            case INSTRUCTION::TYPE::H:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.h_gates++;
                break;

            case INSTRUCTION::TYPE::CX:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.cx_gates++;
                break;

            case INSTRUCTION::TYPE::MSWAP:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.mswap_instructions++;
                break;

            case INSTRUCTION::TYPE::MPREFETCH:
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                stats.mprefetch_instructions++;
                break;

            case INSTRUCTION::TYPE::RX:
            case INSTRUCTION::TYPE::RZ:
                // For rotation gates, count only non-software gates in the unrolled sequence
                for (auto gate_type : inst.urotseq)
                {
                    if (!is_software_instruction(gate_type))
                        stats.unrolled_instructions++;

                    if (gate_type == INSTRUCTION::TYPE::T ||
                        gate_type == INSTRUCTION::TYPE::TDG ||
                        gate_type == INSTRUCTION::TYPE::TX ||
                        gate_type == INSTRUCTION::TYPE::TXDG)
                    {
                        stats.unrolled_t_gates++;
                    }
                    else if (gate_type == INSTRUCTION::TYPE::S ||
                             gate_type == INSTRUCTION::TYPE::SDG ||
                             gate_type == INSTRUCTION::TYPE::SX ||
                             gate_type == INSTRUCTION::TYPE::SXDG)
                    {
                        stats.s_gates++;
                    }
                    else if (gate_type == INSTRUCTION::TYPE::H)
                    {
                        stats.h_gates++;
                    }
                    else if (gate_type == INSTRUCTION::TYPE::CX)
                    {
                        stats.cx_gates++;
                    }
                }
                break;

            default:
                // Other gates count as 1 unrolled instruction if not software
                if (!is_software_instruction(inst.type))
                    stats.unrolled_instructions++;
                break;
        }
        
        // Print progress every 1M instructions
        if (stats.total_instructions % 1000000 == 0)
        {
            std::cout << "[ QS_REPORT ] Processed " << stats.total_instructions << " instructions..." << std::endl;
        }
    }
    
    generic_strm_close(istrm);
    
    std::cout << "[ QS_REPORT ] Analysis complete. Total instructions processed: " << stats.total_instructions << std::endl;
    
    return stats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int main(int argc, char** argv)
{
    std::string input_file;
    
    ARGPARSE()
        .required("input-file", "compressed binary program file (.bin, .gz, .xz)", input_file)
        .parse(argc, argv);
    
    try
    {
        ProgramStats stats = analyze_binary_file(input_file);
        
        // Print the report
        std::cout << "\n";
        std::cout << "PROGRAM REPORT" << std::endl;
        std::cout << "==============" << std::endl;
        
        print_stat_line(std::cout, "PROGRAM_QUBITS", stats.num_qubits);
        print_stat_line(std::cout, "TOTAL_INSTRUCTIONS", stats.total_instructions);
        print_stat_line(std::cout, "UNROLLED_INSTRUCTIONS", stats.unrolled_instructions);
        print_stat_line(std::cout, "UNROLLED_T_GATES", stats.unrolled_t_gates);
        print_stat_line(std::cout, "S_GATES", stats.s_gates);
        print_stat_line(std::cout, "H_GATES", stats.h_gates);
        print_stat_line(std::cout, "CX_GATES", stats.cx_gates);
        print_stat_line(std::cout, "MSWAP_INSTRUCTIONS", stats.mswap_instructions);
        print_stat_line(std::cout, "MPREFETCH_INSTRUCTIONS", stats.mprefetch_instructions);

        // Calculate and print percentages
        if (stats.unrolled_instructions > 0)
        {
            double t_gate_percentage = (static_cast<double>(stats.unrolled_t_gates) / static_cast<double>(stats.unrolled_instructions)) * 100.0;
            double s_gate_percentage = (static_cast<double>(stats.s_gates) / static_cast<double>(stats.unrolled_instructions)) * 100.0;
            double h_gate_percentage = (static_cast<double>(stats.h_gates) / static_cast<double>(stats.unrolled_instructions)) * 100.0;
            double cx_gate_percentage = (static_cast<double>(stats.cx_gates) / static_cast<double>(stats.unrolled_instructions)) * 100.0;
            double memory_instruction_percentage = (static_cast<double>(stats.mswap_instructions + stats.mprefetch_instructions) / static_cast<double>(stats.unrolled_instructions)) * 100.0;

            std::cout << "\n";
            print_stat_line(std::cout, "T_GATE_PERCENTAGE", t_gate_percentage);
            print_stat_line(std::cout, "S_GATE_PERCENTAGE", s_gate_percentage);
            print_stat_line(std::cout, "H_GATE_PERCENTAGE", h_gate_percentage);
            print_stat_line(std::cout, "CX_GATE_PERCENTAGE", cx_gate_percentage);
            print_stat_line(std::cout, "MEMORY_INSTRUCTION_PERCENTAGE", memory_instruction_percentage);
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
