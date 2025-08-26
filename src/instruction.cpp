/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include "instruction.h"

#include <iomanip>
#include <iostream>
#include <sstream>

#define INSTRUCTION_ALSO_SHOW_ROTATION_AS_FLOAT

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::io_encoding::io_encoding(const INSTRUCTION* inst)
    :type_id{static_cast<gate_id_type>(inst->type)},
    urotseq_size{static_cast<uint32_t>(inst->urotseq.size())},
    urotseq{new gate_id_type[urotseq_size]}
{
    // initialize `qubits`
    std::copy(inst->qubits.begin(), inst->qubits.end(), std::begin(qubits));

    // initialize `angle`
    auto words = inst->angle.get_words();
    std::move(words.begin(), words.end(), std::begin(angle));

    // initialize `urotseq`
    std::transform(inst->urotseq.begin(), inst->urotseq.end(), urotseq,
                       [] (TYPE t) { return static_cast<gate_id_type>(t); });
}

INSTRUCTION::io_encoding::~io_encoding()
{
    if (urotseq_size > 0)
        delete[] urotseq;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::INSTRUCTION(TYPE _type, std::vector<qubit_type> _qubits)
    :type{_type},
    qubits(_qubits)
{}

INSTRUCTION::INSTRUCTION(io_encoding&& e)
    :type{static_cast<TYPE>(e.type_id)},
    angle(std::begin(e.angle), std::end(e.angle)),
    urotseq(e.urotseq_size)
{
    auto q_begin = std::begin(e.qubits);
    auto q_end = std::end(e.qubits);
    auto it = std::find(q_begin, q_end, -1);
    qubits = std::vector<qubit_type>(q_begin, it);

    std::transform(e.urotseq, e.urotseq + e.urotseq_size, urotseq.begin(),
                       [] (auto t) { return static_cast<TYPE>(t); });
}

INSTRUCTION::io_encoding
INSTRUCTION::serialize() const
{
    return io_encoding(this);
}

std::string
INSTRUCTION::to_string() const
{
    std::stringstream ss;
    ss << *this;
    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream&
operator<<(std::ostream& os, const INSTRUCTION& inst)
{
    os << BASIS_GATES[static_cast<size_t>(inst.type)];
    if (inst.type == INSTRUCTION::TYPE::RX || inst.type == INSTRUCTION::TYPE::RZ)
    {
        os << "( " << fpa::to_string(inst.angle);
#if defined(INSTRUCTION_ALSO_SHOW_ROTATION_AS_FLOAT)
        os << " = " << convert_fpa_to_float(inst.angle);
#endif
        os << " )";
    }

    for (size_t i = 0; i < inst.qubits.size(); ++i)
        os << " " << inst.qubits[i];

    return os;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////