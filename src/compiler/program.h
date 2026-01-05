/*
    author: Suhas Vittal
    date:   21 August 2025
*/

#ifndef COMPILER_PROGRAM_h
#define COMPILER_PROGRAM_h

#include "generic_io.h"
#include "instruction.h"
#include "compiler/program/rotation_manager.h"

#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

extern int64_t GL_PRINT_PROGRESS;

struct EXPRESSION
{
    enum class OPERATOR { ADD, SUBTRACT, MULTIPLY, DIVIDE };

    using expr_ptr = std::shared_ptr<EXPRESSION>;
    using generic_value_type = std::variant<int64_t, double, std::string, expr_ptr>; 
    using exponential_value_type = std::pair<std::vector<generic_value_type>, bool>;  // sequence, negate
    using term_type = std::vector<std::pair<exponential_value_type, OPERATOR>>;       // sequence, operator (to be used with previous expval)

    std::vector<std::pair<term_type, OPERATOR>> termseq;  // sequence, operator (to be used with previous term)

    std::string to_string() const;
};

struct QASM_INST_INFO
{
    struct operand_type
    {
        constexpr static ssize_t NO_INDEX{-1};

        std::string name;
        ssize_t index{NO_INDEX};
    };

    std::string gate_name;
    std::vector<EXPRESSION> params;
    std::vector<operand_type> args;

    bool is_conditional{false};

    std::string to_string() const;
};

struct REGISTER
{
    enum class TYPE { QUBIT, BIT };

    size_t      id_offset{0};
    TYPE        type{TYPE::QUBIT};
    std::string name;
    size_t      width{1};
};

struct GATE_DEFINITION
{
    std::string name;
    std::vector<std::string> params;
    std::vector<std::string> args;
    std::vector<QASM_INST_INFO> body;
};

}   // namespace prog

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class PROGRAM_INFO
{
public:
    constexpr static size_t MAX_INST_BEFORE_FLUSH{4*1024*1024};

    using fpa_type = INSTRUCTION::fpa_type;

    using register_table = std::unordered_map<std::string, prog::REGISTER>;
    using gate_decl_table = std::unordered_map<std::string, prog::GATE_DEFINITION>;

    struct stats_type
    {
        uint64_t total_gate_count{0};
        uint64_t software_gate_count{0};
        uint64_t t_gate_count{0};
        uint64_t cxz_gate_count{0};  // number of cx/cz gates

        uint64_t rotation_count{0};
        uint64_t ccxz_count{0};      // number of ccx/ccz gates

        uint64_t virtual_inst_count{0};
        uint64_t unrolled_inst_count{0};

        // TODO: add more statistics

        void merge(const stats_type& other);
        void generate_calculated_stats();  // use this to compute means, etc.
    };

    // `final_stats_` is the aggregate statistics for the entire program.
    // this is only set 
    stats_type final_stats_{};

    std::string version_;
private:
    register_table  registers_;
    gate_decl_table user_defined_gates_;

    std::vector<INSTRUCTION> instructions_;

    // this is a cache of the rotation sequences for the rotations that have been synthesized
    // this is used to avoid re-synthesizing the same rotation sequence multiple times
    std::unordered_map<INSTRUCTION::fpa_type, std::vector<INSTRUCTION::TYPE>> rotation_cache_;

    size_t num_qubits_declared_{0};
    size_t num_bits_declared_{0};

    generic_strm_type* ostrm_p_{nullptr};

    uint64_t inst_read_{0};

    bool has_qubit_count_been_written_{false};
public:
    PROGRAM_INFO(generic_strm_type* ostrm_p=nullptr);
    static PROGRAM_INFO from_file(std::string);

    // returns the final program statistics:
    static stats_type read_from_file_and_write_to_binary(std::string, std::string);
    /*
        These are the public member functions that are used to build the program from the Bison parser.
    */
    void add_instruction(prog::QASM_INST_INFO&&);
    void declare_register(prog::REGISTER&&);
    void declare_gate(prog::GATE_DEFINITION&&);
    void merge(PROGRAM_INFO&&);
    /*
        Basic optimizations:
    */
    size_t dead_gate_elimination();  // returns the number of gates removed

    stats_type analyze_program() const;

    void flush_and_clear_instructions();
    const std::vector<INSTRUCTION>& get_instructions() const { return instructions_; }
    size_t get_num_qubits() const { return num_qubits_declared_; }
private:
    qubit_type get_qubit_id_from_operand(const prog::QASM_INST_INFO::operand_type&) const;

    void   complete_rotation_gates();
    size_t dead_gate_elim_pass(size_t prev_gates_removed=0);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t get_required_precision(const INSTRUCTION::fpa_type&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // COMPILER_PROGRAM_h
