/*
    author: Suhas Vittal
    date:   21 August 2025
*/

#ifndef COMPILER_PROGRAM_h
#define COMPILER_PROGRAM_h

#include "generic_io.h"
#include "instruction.h"
#include "compiler/program/expression.h"
#include "compiler/program/rotation_manager.h"

#include "instruction_fpa_hash.inl"

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

/*
 * Represents a qubit or classical bit operand in a QASM instruction.
 * Can reference either a single qubit/bit or an entire register.
 * */
struct QASM_OPERAND
{
    constexpr static ssize_t NO_INDEX{-1};

    std::string name;
    ssize_t index{NO_INDEX};
};

/*
 * Contains information about a given QASM instruction.
 * This is eventaully converted into the basis gates
 * we have defined in `instruction.h`
 * */
struct QASM_INST_INFO
{
    std::string              gate_name;
    std::vector<EXPRESSION>  params;
    std::vector<QASM_OPERAND> args;

    bool is_conditional{false};

    std::string to_string() const;
};

/*
 * Register information.
 * */
struct REGISTER
{
    enum class TYPE { QUBIT, BIT };

    size_t      id_offset{0};
    TYPE        type{TYPE::QUBIT};
    std::string name;
    size_t      width{1};
};


/*
 * `GATE_DEFINITION` stores custom gate
 * definitions, such as those in `qelib1.inc`.
 * */
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
    /*
     * This controls the number of instructions before writing
     * to the output stream.
     *
     * A larger number is better since we spend less time waiting
     * on synthesis and we have a larger window for optimizations,
     * but is more memory intensive.
     * */
    constexpr static size_t MAX_INST_BEFORE_FLUSH{4*1024*1024};

    using fpa_type = INSTRUCTION::fpa_type;
    using register_table = std::unordered_map<std::string, prog::REGISTER>;
    using gate_decl_table = std::unordered_map<std::string, prog::GATE_DEFINITION>;
    using inst_ptr = std::unique_ptr<INSTRUCTION>;

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

        void merge(const stats_type& other);
    };

    /*
     * `final_stats` is the aggregate statistics for the entire program.
     * This is only set after everything is done.
     * */
    stats_type final_stats{};

    /*
     * OpenQASM version (2.0 is only supported, so don't use 3.0)
     * */
    std::string version;
private:
    register_table        registers_;
    gate_decl_table       user_defined_gates_;
    std::vector<inst_ptr> instructions_;

    /*
     * This is a cache of the rotation sequences for the rotations that have been synthesized.
     * This is used to avoid re-synthesizing the same rotation sequence multiple times
     * */
    std::unordered_map<INSTRUCTION::fpa_type, std::vector<INSTRUCTION::TYPE>> rotation_cache_;

    size_t num_qubits_declared_{0};
    size_t num_bits_declared_{0};

    /*
     * IO data structures and tracking:
     * */
    generic_strm_type* ostrm_p_{nullptr};
    uint64_t           inst_read_{0};
    bool               has_qubit_count_been_written_{false};
public:
    PROGRAM_INFO(generic_strm_type* ostrm_p=nullptr);
    static PROGRAM_INFO from_file(std::string);

    /*
     * Compiles the input qasm file. Returns program statistics for the compilation (i.e., number
     * of gates).
     * */
    static stats_type read_from_file_and_write_to_binary(std::string, std::string);

    /*
     * These are the public member functions used to build the program representation from
     * the Bison parser (see `src/compiler/program/oq2/parser.y`)
     * */
    void add_instruction(prog::QASM_INST_INFO&&);
    void declare_register(prog::REGISTER&&);
    void declare_gate(prog::GATE_DEFINITION&&);
    void merge(PROGRAM_INFO&&);

    /*
     * We implement basic optimizations to eliminate useless gates (via gate cancellation and
     * removal of identity gates).
     * */
    size_t dead_gate_elimination();  // returns the number of gates removed

    /*
     * Dumps instructions into the output stream at `*ostrm_p_`
     * */
    void flush_and_clear_instructions();

    const std::vector<inst_ptr>& get_instructions() const { return instructions_; }
    size_t                       get_num_qubits() const { return num_qubits_declared_; }
private:
    qubit_type get_qubit_id_from_operand(const prog::QASM_OPERAND&) const;

    /*
     * `process_rotation_gate` evaluates the symbolic expression in `angle_expr` and
     * schedules the synthesis for the given rotation. It returns the evaluated
     * expression as a fixed point value.
     * */
    fpa_type process_rotation_gate(INSTRUCTION::TYPE, const prog::EXPRESSION& angle_expr);

    /*
     * `add_scalar_instruction` and `add_vector_instruction` update `instructions_` with new
     * instructions. The only difference is that `add_vector_instruction` adds multiple instructions
     * at once (one per vector register width).
     * */
    void add_scalar_instruction(INSTRUCTION::TYPE, const std::vector<prog::QASM_OPERAND>&, fpa_type);
    void add_vector_instruction(INSTRUCTION::TYPE, 
                                    prog::QASM_INST_INFO&, 
                                    fpa_type, 
                                    size_t width, 
                                    const std::vector<bool>& v_op_vec, 
                                    const std::vector<size_t>& v_op_width);

    /*
     * `expand_user_defined_gate` handles the expansion of user-defined gates (such as those
     * in qelib1.inc)
     * */
    void expand_user_defined_gate(prog::QASM_INST_INFO&&);
    void add_basis_gate_instruction(prog::QASM_INST_INFO&&, INSTRUCTION::TYPE);

    /*
     * These are both helper functions for `dead_gate_elimination`.
     *
     * `cancel_adjacent_rotations` is gate cancellation for RZ and RX gates.
     * `cancel_inverse_gate_pairs` kills pairs of gates that are self inverses.
     * */
    void cancel_adjacent_rotations();
    void cancel_inverse_gate_pairs();

    void   complete_rotation_gates();
    size_t dead_gate_elim_pass(size_t prev_gates_removed=0);

    /*
     * Returns the statistics for the current `instructions_` contents.
     * */
    stats_type compute_statistics_for_current_instructions() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // COMPILER_PROGRAM_h
