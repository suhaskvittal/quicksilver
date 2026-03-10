/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include <cassert>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class ITER_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type, ITER_TYPE q_begin, ITER_TYPE q_end)
    :type{_type},
    qubits(q_begin, q_end),
    angle{},
    urotseq{},
    qubit_count{qubits.size()}
{
    assert(get_inst_qubit_count(_type) == 0
           || std::distance(q_begin, q_end) == (ptrdiff_t)get_inst_qubit_count(_type));
}

template <class ITER_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type,
                         std::initializer_list<qubit_type> qubits_init,
                         fpa_type _angle,
                         ITER_TYPE urotseq_begin,
                         ITER_TYPE urotseq_end)
    :type{_type},
    qubits(qubits_init.begin(), qubits_init.end()),
    angle{_angle},
    urotseq(urotseq_begin, urotseq_end),
    qubit_count{qubits.size()}
{
    assert(get_inst_qubit_count(_type) == 0
           || (ptrdiff_t)qubits_init.size() == (ptrdiff_t)get_inst_qubit_count(_type));
    if (uop_count() > 0)
        get_next_uop();
}

template <class Q_IT_TYPE, class U_IT_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type,
                         Q_IT_TYPE q_begin, Q_IT_TYPE q_end,
                         fpa_type _angle,
                         U_IT_TYPE urotseq_begin,
                         U_IT_TYPE urotseq_end)
    :type{_type},
    qubits(q_begin, q_end),
    angle{_angle},
    urotseq(urotseq_begin, urotseq_end),
    qubit_count{qubits.size()}
{
    assert(get_inst_qubit_count(_type) == 0
           || std::distance(q_begin, q_end) == (ptrdiff_t)get_inst_qubit_count(_type));
    if (uop_count() > 0)
        get_next_uop();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr bool
is_software_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::X
            || t == INSTRUCTION::TYPE::Y
            || t == INSTRUCTION::TYPE::Z
            || t == INSTRUCTION::TYPE::SWAP;
}

constexpr bool
is_memory_access(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::LOAD 
            || t == INSTRUCTION::TYPE::STORE 
            || t == INSTRUCTION::TYPE::COUPLED_LOAD_STORE;
}

constexpr bool
is_s_like_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::S
            || t == INSTRUCTION::TYPE::SX
            || t == INSTRUCTION::TYPE::SDG
            || t == INSTRUCTION::TYPE::SXDG;
}

constexpr bool
is_t_like_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::T
            || t == INSTRUCTION::TYPE::TX
            || t == INSTRUCTION::TYPE::TDG
            || t == INSTRUCTION::TYPE::TXDG;
}

constexpr bool
is_rotation_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::RX || t == INSTRUCTION::TYPE::RZ;
}

constexpr bool
is_cx_like_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::CX || t == INSTRUCTION::TYPE::CZ;
}

constexpr bool
is_toffoli_like_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::CCX || t == INSTRUCTION::TYPE::CCZ;
}

constexpr bool
is_clifford_pauli_rotation(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::PAULI_ROTATION_PI
            || t == INSTRUCTION::TYPE::PAULI_ROTATION_H_PI
            || t == INSTRUCTION::TYPE::PAULI_ROTATION_H_PI_DAG;
}

constexpr bool
is_non_clifford_puali_rotation(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::PAULI_ROTATION_Q_PI
            || t == INSTRUCTION::TYPE::PAULI_ROTATION_Q_PI_DAG;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t
get_inst_qubit_count(INSTRUCTION::TYPE t)
{
    switch (t)
    {
        // 1-qubit gates
        case INSTRUCTION::TYPE::H:
        case INSTRUCTION::TYPE::X:
        case INSTRUCTION::TYPE::Y:
        case INSTRUCTION::TYPE::Z:
        case INSTRUCTION::TYPE::S:
        case INSTRUCTION::TYPE::SX:
        case INSTRUCTION::TYPE::SDG:
        case INSTRUCTION::TYPE::SXDG:
        case INSTRUCTION::TYPE::T:
        case INSTRUCTION::TYPE::TX:
        case INSTRUCTION::TYPE::TDG:
        case INSTRUCTION::TYPE::TXDG:
        case INSTRUCTION::TYPE::RX:
        case INSTRUCTION::TYPE::RZ:
        case INSTRUCTION::TYPE::MZ:
        case INSTRUCTION::TYPE::MX:
        case INSTRUCTION::TYPE::LOAD:
        case INSTRUCTION::TYPE::STORE:
            return 1;

        // 2-qubit gates
        case INSTRUCTION::TYPE::CX:
        case INSTRUCTION::TYPE::CZ:
        case INSTRUCTION::TYPE::SWAP:
        case INSTRUCTION::TYPE::COUPLED_LOAD_STORE:
            return 2;

        // 3-qubit gates
        case INSTRUCTION::TYPE::CCX:
        case INSTRUCTION::TYPE::CCZ:
            return 3;

        // No-op
        case INSTRUCTION::TYPE::NIL:
            return 0;
    }
    return 0;  // default case
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
