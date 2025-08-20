/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#include "instruction.h"

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::io_encoding::io_encoding(INSTRUCTION* inst)
    :ip{inst->ip},
    inst_number{inst->inst_number},
    type_id{static_cast<gate_id_type>(inst->type)},
    urotseq_size{inst->urotseq.size()},
    urotseq{new gate_id_type[urotseq_size]}
{
    // initialize `qubits`
    std::copy(inst->qubits.begin(), inst->qubits.end(), std::begin(qubits));

    // initialize `angle`
    auto words = inst->angle.get_words();
    std::move(words.begin(), words.end(), std::begin(angle));

    // initialize `urotseq`
    std::copy(inst->urotseq.begin(), inst->urotseq.end(), urotseq);
}

INSTRUCTION::io_encoding::~io_encoding()
{
    delete[] urotseq;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::INSTRUCTION(uint64_t _ip, uint64_t _inst_number, TYPE _type, std::initializer_list<qubit_type> _qubits)
    :ip{_ip},
    inst_number{_inst_number},
    type{_type},
    qubits(_qubits)
{}

INSTRUCTION::INSTRUCTION(io_encoding e)
    :ip{e.ip},
    inst_number{e.inst_number},
    type{static_cast<TYPE>(e.type_id)},
    qubits(std::begin(e.qubits), std::end(e.qubits)),
    angle(std::begin(e.angle), std::end(e.angle)),
    unrolled_rotation_sequence(e.urotseq, e.urotseq + e.urotseq_size)
{}

INSTRUCTION::io_encoding
INSTRUCTION::serialize() const
{
    return io_encoding(this);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////