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
    qubits(convert_qubit_container_into_qubit_array(_type, q_begin, q_end)),
    angle{},
    urotseq{},
    qubit_count{get_inst_qubit_count(_type)}
{}

template <class ITER_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type,
                         std::initializer_list<qubit_type> qubits_init,
                         fpa_type _angle,
                         ITER_TYPE urotseq_begin,
                         ITER_TYPE urotseq_end)
    :type{_type},
    qubits(convert_qubit_container_into_qubit_array(_type, qubits_init.begin(), qubits_init.end())),
    angle{_angle},
    urotseq(urotseq_begin, urotseq_end),
    qubit_count{get_inst_qubit_count(_type)}
{}

template <class Q_IT_TYPE, class U_IT_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type,
                         Q_IT_TYPE q_begin, Q_IT_TYPE q_end,
                         fpa_type _angle,
                         U_IT_TYPE urotseq_begin,
                         U_IT_TYPE urotseq_end)
    :type{_type},
    qubits(convert_qubit_container_into_qubit_array(_type, q_begin, q_end)),
    angle{_angle},
    urotseq(urotseq_begin, urotseq_end),
    qubit_count{get_inst_qubit_count(_type)}
{}

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
    return t == INSTRUCTION::TYPE::MSWAP;
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
    return t == INSTRUCTION::TYPE::RX
            || t == INSTRUCTION::TYPE::RZ;
}

constexpr bool
is_toffoli_like_instruction(INSTRUCTION::TYPE t)
{
    return t == INSTRUCTION::TYPE::CCX
            || t == INSTRUCTION::TYPE::CCZ;
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
            return 1;

        // 2-qubit gates
        case INSTRUCTION::TYPE::CX:
        case INSTRUCTION::TYPE::CZ:
        case INSTRUCTION::TYPE::SWAP:
        case INSTRUCTION::TYPE::MSWAP:
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

template <class ITER> INSTRUCTION::qubit_array 
convert_qubit_container_into_qubit_array(INSTRUCTION::TYPE type, ITER begin, ITER end)
{
    assert(std::distance(begin, end) == get_inst_qubit_count(type));

    INSTRUCTION::qubit_array qubits;
    std::copy(begin, end, qubits.begin());
    return qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
