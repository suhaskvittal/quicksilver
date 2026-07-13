/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#include <cassert>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class IterType>
Instruction::Instruction(Type _type, IterType q_begin, IterType q_end)
    :type{_type},
    qubits(q_begin, q_end),
    angle{},
    urotseq{},
    qubit_count{qubits.size()}
{
    assert(get_inst_qubit_count(_type) == 0
           || std::distance(q_begin, q_end) == (ptrdiff_t)get_inst_qubit_count(_type));
}

template <class IterType>
Instruction::Instruction(Type _type,
                         std::initializer_list<qubit_type> qubits_init,
                         fpa_type _angle,
                         IterType urotseq_begin,
                         IterType urotseq_end)
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

template <class QIt, class UIt>
Instruction::Instruction(Type _type,
                         QIt q_begin, QIt q_end,
                         fpa_type _angle,
                         UIt urotseq_begin,
                         UIt urotseq_end)
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
is_software_instruction(Instruction::Type t)
{
    return t == Instruction::Type::X
            || t == Instruction::Type::Y
            || t == Instruction::Type::Z
            || t == Instruction::Type::SWAP;
}

constexpr bool
is_memory_access(Instruction::Type t)
{
    return t == Instruction::Type::LOAD 
            || t == Instruction::Type::STORE 
            || t == Instruction::Type::COUPLED_LOAD_STORE;
}

constexpr bool
is_s_like_instruction(Instruction::Type t)
{
    return t == Instruction::Type::S
            || t == Instruction::Type::SX
            || t == Instruction::Type::SDG
            || t == Instruction::Type::SXDG;
}

constexpr bool
is_t_like_instruction(Instruction::Type t)
{
    return t == Instruction::Type::T
            || t == Instruction::Type::TX
            || t == Instruction::Type::TDG
            || t == Instruction::Type::TXDG;
}

constexpr bool
is_rotation_instruction(Instruction::Type t)
{
    return t == Instruction::Type::RX || t == Instruction::Type::RZ;
}

constexpr bool
is_cx_like_instruction(Instruction::Type t)
{
    return t == Instruction::Type::CX || t == Instruction::Type::CZ;
}

constexpr bool
is_toffoli_like_instruction(Instruction::Type t)
{
    return t == Instruction::Type::CCX || t == Instruction::Type::CCZ;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr size_t
get_inst_qubit_count(Instruction::Type t)
{
    switch (t)
    {
        // 1-qubit gates
        case Instruction::Type::H:
        case Instruction::Type::X:
        case Instruction::Type::Y:
        case Instruction::Type::Z:
        case Instruction::Type::S:
        case Instruction::Type::SX:
        case Instruction::Type::SDG:
        case Instruction::Type::SXDG:
        case Instruction::Type::T:
        case Instruction::Type::TX:
        case Instruction::Type::TDG:
        case Instruction::Type::TXDG:
        case Instruction::Type::RX:
        case Instruction::Type::RZ:
        case Instruction::Type::MZ:
        case Instruction::Type::MX:
        case Instruction::Type::LOAD:
        case Instruction::Type::STORE:
            return 1;

        // 2-qubit gates
        case Instruction::Type::CX:
        case Instruction::Type::CZ:
        case Instruction::Type::SWAP:
        case Instruction::Type::COUPLED_LOAD_STORE:
            return 2;

        // 3-qubit gates
        case Instruction::Type::CCX:
        case Instruction::Type::CCZ:
            return 3;

        // No-op
        case Instruction::Type::NIL:
            return 0;
    }
    return 0;  // default case
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
