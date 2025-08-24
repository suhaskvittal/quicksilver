/*
    author: Suhas Vittal
    date:   21 August 2025
*/

#ifndef PROGRAM_h
#define PROGRAM_h

#include "instruction.h"

#include <string>
#include <unordered_map>
#include <memory>
#include <variant>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

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
    constexpr static size_t FPA_PRECISION = INSTRUCTION::FPA_PRECISION;

    using fpa_type = FPA_TYPE<FPA_PRECISION>;

    using register_table = std::unordered_map<std::string, prog::REGISTER>;
    using gate_decl_table = std::unordered_map<std::string, prog::GATE_DEFINITION>;

    std::string version_;
private:
    register_table  registers_;
    gate_decl_table user_defined_gates_;

    std::vector<INSTRUCTION> instructions_;

    uint64_t ip_{0};
    size_t num_qubits_declared_{0};
    size_t num_bits_declared_{0};
public:
    PROGRAM_INFO() =default;

    static PROGRAM_INFO from_file(std::string);

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

    const std::vector<INSTRUCTION>& get_instructions() const { return instructions_; }
private:
    qubit_type get_qubit_id_from_operand(const prog::QASM_INST_INFO::operand_type&) const;

    /*
        This is a private member function so that we can update a "cache" of rotation sequences.
    */
    std::vector<INSTRUCTION::TYPE> unroll_rotation(fpa_type);

    size_t dead_gate_elim_pass(size_t prev_gates_removed=0);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif