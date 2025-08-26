/*
    author: Suhas Vittal
    date:   21 August 2025
*/

#ifndef PROGRAM_h
#define PROGRAM_h

#include "instruction.h"

#include <memory>
#include <numeric>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// Implementation of `std::hash` for `fpa_type`
namespace std
{

template <>
struct hash<INSTRUCTION::fpa_type>
{
    using value_type = INSTRUCTION::fpa_type;

    size_t
    operator()(const value_type& x) const 
    { 
        const auto& words = x.get_words();
        uint64_t out = std::reduce(words.begin(), words.end(), uint64_t{0},
                            [] (uint64_t acc, uint64_t word) { return acc ^ word; });
        return out;
    }
};

}   // namespace std

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
    using fpa_type = INSTRUCTION::fpa_type;

    using register_table = std::unordered_map<std::string, prog::REGISTER>;
    using gate_decl_table = std::unordered_map<std::string, prog::GATE_DEFINITION>;

    using rotation_cache_type = std::unordered_map<fpa_type, std::vector<INSTRUCTION::TYPE>>;

    struct stats_type
    {
        uint64_t software_gate_count{0};
        uint64_t t_gate_count{0};
        uint64_t cxz_gate_count{0};  // number of cx/cz gates

        uint64_t rotation_count{0};
        uint64_t ccxz_count{0};      // number of ccx/ccz gates

        uint64_t virtual_inst_count{0};
        uint64_t unrolled_inst_count{0};

        /*
            These have to be calculated later:
        */
        double   mean_instruction_level_parallelism{};
        double   mean_concurrent_rotation_count{};
        double   mean_concurrent_cxz_count{};
        double   mean_rotation_unrolled_count{};

        uint64_t max_instruction_level_parallelism{0};
        uint64_t max_concurrent_rotation_count{0};
        uint64_t max_concurrent_cxz_count{0};
        uint64_t max_rotation_unrolled_count{0};
    };

    constexpr static ssize_t USE_MSB_TO_DETERMINE_UROT_PRECISION{-1};

    std::string version_;
private:
    register_table  registers_;
    gate_decl_table user_defined_gates_;

    std::vector<INSTRUCTION> instructions_;

    rotation_cache_type rotation_cache_{};

    size_t num_qubits_declared_{0};
    size_t num_bits_declared_{0};

    ssize_t urot_precision_{USE_MSB_TO_DETERMINE_UROT_PRECISION};
public:
    PROGRAM_INFO(ssize_t urot_precision=USE_MSB_TO_DETERMINE_UROT_PRECISION);

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

    stats_type analyze_program() const;

    const std::vector<INSTRUCTION>& get_instructions() const { return instructions_; }
    size_t get_num_qubits() const { return num_qubits_declared_; }
private:
    qubit_type get_qubit_id_from_operand(const prog::QASM_INST_INFO::operand_type&) const;

    /*
        This is a private member function so that we can update a "cache" of rotation sequences.
    */
    std::vector<INSTRUCTION::TYPE> unroll_rotation(fpa_type);

    size_t dead_gate_elim_pass(size_t prev_gates_removed=0);
    /*
        Templated function for perform some operations every layer. The callback is called
        every layer, and is passed the layer number (`size_t`) 
        and the instructions (std::vector<const INSTRUCTION*>) in that layer. Note that the 
        instructions are pointers to the instructions in the original program, and not copies.
    */
    template <class CALLBACK> void iterate_through_instructions_by_layer(const CALLBACK&) const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class CALLBACK> void
PROGRAM_INFO::iterate_through_instructions_by_layer(const CALLBACK& callback) const
{
    struct layer_type
    {
        std::vector<const INSTRUCTION*> inst;
        size_t num_qubits_among_inst{0};
    };

    // need this to track dependencies roughly
    std::vector<size_t> qubit_last_used_layer(num_qubits_declared_, 0);

    // a layer is removed once `num_qubits_among_inst == num_qubits_declared_`
    std::unordered_map<size_t, layer_type> layers;
    layers[0] = layer_type{};

    for (const auto& inst : instructions_)
    {
        // compute next layer id
        std::vector<size_t> layer_ids(inst.qubits.size());
        std::transform(inst.qubits.begin(), inst.qubits.end(), layer_ids.begin(), 
                    [&qubit_last_used_layer] (qubit_type qubit_id)
                    {
                        return qubit_last_used_layer[qubit_id];
                    });
        auto max_it = std::max_element(layer_ids.begin(), layer_ids.end());
        size_t next_layer_id = (*max_it) + 1;
    
        auto layer_it = layers.find(next_layer_id);
        if (layer_it == layers.end())
        {
            // time to make a new layer:
            layer_type lay{};
            lay.inst.push_back(&inst);
            lay.num_qubits_among_inst += inst.qubits.size();
            layers[next_layer_id] = lay;
        }
        else
        {
            layer_it->second.inst.push_back(&inst);
            layer_it->second.num_qubits_among_inst += inst.qubits.size();

            // if the layer is full, issue a callback and delete `layer_it`
            if (layer_it->second.num_qubits_among_inst == num_qubits_declared_)
            {
                callback(layer_it->first, layer_it->second.inst);
                layers.erase(layer_it);
            }
        }

        // update `qubit_last_used_layer`
        for (const auto& qubit_id : inst.qubits)
            qubit_last_used_layer[qubit_id] = next_layer_id;
    }

    // issue the callback for any remaining layers:
    for (const auto& [layer_id, layer] : layers)
        callback(layer_id, layer.inst);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif