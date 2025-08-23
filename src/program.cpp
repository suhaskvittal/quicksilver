/*
    author: Suhas Vittal
    date:   22 August 2025
*/

#include "program.h"

using namespace prog;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> std::unordered_map<std::string, T>
_make_substitution_map(const std::vector<std::string>& names, const std::vector<T>& values)
{
    std::unordered_map<std::string, T> subst_map;
    for (size_t i = 0; i < names.size(); ++i)
        subst_map[names[i]] = values[i];
    return subst_map;
}

void 
PROGRAM_INFO::add_instruction(QASM_INST_INFO&& qasm_inst)
{
    // need to basis gate translation:
    constexpr std::string_view BASIS_GATES[] 
    {
        "h", "x", "y", "z", "s", "t", "sdg", "tdg", "cx", "cz", "rx", "ry", "rz", "ccx", "measure"
    };

    auto basis_gate_it = std::find(std::begin(BASIS_GATES), std::end(BASIS_GATES), qasm_inst.gate_name);
    if (basis_gate_it != std::end(BASIS_GATES))
    {
        // simple solution:
        INSTRUCTION::TYPE inst_type = static_cast<INSTRUCTION::TYPE>(
                                            std::distance(std::begin(BASIS_GATES), basis_gate_it));
        // TODO: handle rotation gates:
        
        // convert operands to qubits:
        std::vector<qubit_type> qubits(qasm_inst.args.size());
        std::transform(qasm_inst.args.begin(), qasm_inst.args.end(), qubits.begin(), 
                    [] (const auto& x) { return get_qubit_id_from_operand(x); });

        INSTRUCTION inst(ip_, instructions_.size(), inst_type, qubits);
        instructions_.push_back(inst);
    }
    else
    {
        // check for gate definition:
        auto gate_it = user_defined_gates_.find(qasm_inst.gate_name);
        if (gate_it == user_defined_gates_.end())
            throw std::runtime_error("gate not defined: " + qasm_inst.gate_name);

        // get gate definition:
        const auto& gate_def = gate_it->second;

        if (gate_def.params.size() != qasm_inst.params.size())
        {
            throw std::runtime_error("expected " + std::to_string(qasm_inst.params.size()) + " but only got "
                                    + std::to_string(gate_def.params.size()) + ": "
                                    + to_string(qasm_inst));
        }

        if (gate_def.args.size() != qasm_inst.args.size())
        {
            throw std::runtime_error("expected " + std::to_string(qasm_inst.args.size()) + " but only got "
                                    + std::to_string(gate_def.args.size()) + ": "
                                    + to_string(qasm_inst));
        }

        auto param_subst_map = _make_substitution_map(gate_def.params, qasm_inst.params)
        auto arg_subst_map = _make_substitution_map(gate_def.args, qasm_inst.args);

        for (const auto& q_inst : gate_def.body)
        {
            // need to do parameter and argument substitution:
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

qubit_type 
PROGRAM_INFO::get_qubit_id_from_operand(const QASM_INST_INFO::operand_type& operand) const
{
    // get register:
    auto it = registers_.find(operand.name);
    if (it == registers_.end())
        throw std::runtime_error("register not found: " + operand.name);

    const auto& r = it->second;
    qubit_type idx = r.offset;

    // verify that the offset is within the register:
    if (operand.index >= 0 && operand.index >= r.width)
        throw std::runtime_error("operand index out of bounds: " + to_string(operand));

    return idx + std::max(operand.index, 0);  // handle the case where the index is negative (NO_INDEX)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////