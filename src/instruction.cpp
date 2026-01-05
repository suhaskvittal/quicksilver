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

/*
 * Anonymous helper functions (implemented at bottom of the file)
 * */

namespace
{

constexpr size_t NUM_CCZ_UOPS{13};
constexpr size_t NUM_CCX_UOPS{NUM_CCZ_UOPS+2};

/*
 * This is a POD for IO operations
 * with `INSTRUCTION`. We read/write data into here,
 * and then convert it into an `INSTRUCTION`.
 * */
struct io_encoding
{
    constexpr size_t MAX_QUBITS{INSTRUCTION::MAX_QUBITS};
    constexpr size_t UROTSEQ_CAPACITY{256};

    using fpa_type = INSTRUCTION::fpa_type;

    uint8_t             type_id;
    qubit_type          qubits[MAX_QUBITS];
    uint16_t            fpa_word_count{fpa_type::NUM_WORDS};  // needed in case `FPA_PRECISION` changes
    fpa_type::word_type angle[fpa_type::NUM_WORDS];
    uint16_t            urotseq_size;
    uint8_t             urotseq[UROTSEQ_CAPACITY];
};

/*
 * `_fill_or_consume_serialized_instruction` either sets the data in `io_encoding` (if using an input stream),
 *  or writes its data to a file (output stream)
 * */
template <class IO_FUNCTION>
void _fill_or_consume_serialized_instruction(io_encoding&, generic_strm_type&, const IO_FUNCTION&);

}  // anon namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::INSTRUCTION(TYPE _type, std::initializer_list<qubit_type> _qubits)
    :type{_type},
    qubits(_convert_qubit_container_into_qubit_array(_type, _qubits.begin(), _qubits.end())),
    angle{},
    urotseq{},
    qubit_count_{get_inst_qubit_count(_type)}
{
    if (uop_count() > 0)
        get_next_uop();
}

INSTRUCTION::~INSTRUCTION()
{
    if (current_uop_ != nullptr)
        delete current_uop_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
INSTRUCTION::retire_current_uop()
{
    if (current_uop_ == nullptr)
        std::cerr << "INSTRUCTION::retire_current_uop: tried to retire current uop, but does not exist" << _die{};

    uops_retired_++;
    if (uops_retired_ >= uop_count())
        return true;

    delete current_uop_;
    get_next_uop();

    return false;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t       INSTRUCTION::uops_retired() const { return uops_retired_; }
INSTRUCTION* INSTRUCTION::current_uop() const { return current_uop_; }

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

qubit_type*       INSTRUCTION::q_begin() { return qubits.data(); }
qubit_type*       INSTRUCTION::q_end() { return qubits.data() + qubit_count_; }

const qubit_type* INSTRUCTION::q_begin() const { return qubits.data(); }
const qubit_type* INSTRUCTION::q_end() const { return qubits.data() + qubit_count_; }

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
INSTRUCTION::uop_count() const
{

    if (is_rotation_instruction(type))
        return urotseq.size();
    else if (type == INSTRUCTION::TYPE::CCX)
        return NUM_CCX_UOPS;
    else if (type == INSTRUCTION::TYPE::CCZ)
        return NUM_CCZ_UOPS;
    else
        return 0;
}

size_t
INSTRUCTION::unrolled_inst_count() const
{
    return std::max(size_t{1}, uop_count());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
INSTRUCTION::to_string() const
{
    std::stringstream ss;
    ss << *this;
    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
INSTRUCTION::get_next_uop()
{
    if (is_rotation_instruction(type))
    {
        current_uop_ = new INSTRUCTION{urotseq[uops_retired_], {qubits[0}};
    }
    else
    {
        // we define the uop order for CCX and CCZ gates here (CCX has two extra H gates -- front and back).
        using uop_spec_type = std::pari<INSTRUCTION::TYPE, std::array<ssize_t,2>>:
        constexpr INSTRUCTION::TYPE CX = INSTRUCTION::TYPE::CX;
        constexpr INSTRUCTION::TYPE TDG = INSTRUCTION::TYPE::TDG;
        constexpr INSTRUCTION::TYPE T = INSTRUCTION::TYPE::T;
        constexpr uop_spec_type CCZ_UOPS[]
        {
            {CX, {1,2}},        // The argument here is { <type>, <qubit-indices> }
            {TDG, {2,-1}},      // if an index is -1, then we don't have a qubit (single qubit instruction)
            {CX, {0,2}},
            {T, {2,-1}},
            {CX, {1,2}},
            {T, {1,-1}},
            {TDG, {2,-1}},
            {CX, {0,2}},
            {T, {2,-1}},
            {CX, {0,1}},
            {T, {0,-1}},
            {TDG, {1,-1}},
            {CX, {0,1}}
        };

        // we can just index into `CCZ_UOPS` to get what we want:
        size_t uop_idx{uops_retired_};

        // handle special case when this is a CCX gate and we are looking at the first
        // or last UOP
        if (type == INSTRUCTION::TYPE::CCX)
        {
            if (uop_idx == 0 || uop_idx == NUM_CCX_UOPS-1)
            {
                current_uop_ = new INSTRUCTION{INSTRUCTION::TYPE::H, {qubits[2]}};
                return false;
            }

            // if we survive until this point, decrement `uop_idx` so we can index into `CCZ_UOPS`
            // correctly
            uop_idx--;
        }

        auto [uop_type, uop_qubit_idx] = CCZ_UOPS[uop_idx];
        std::vector<qubit_type> uop_args;
        uop_args.reserve(2);
        for (ssize_t ii : uop_qubit_idx)
            if (ii >= 0)
                uop_args.push_back(qubits[ii]);
        current_uop_ = new INSTRUCTION{uop_type, uop_args};
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream&
operator<<(std::ostream& os, const INSTRUCTION& inst)
{
    os << BASIS_GATES[static_cast<size_t>(inst.type)];
    if (is_rotation_instruction(inst.type))
    {
        os << "( " << fpa::to_string(inst.angle);
#if defined(INSTRUCTION_ALSO_SHOW_ROTATION_AS_FLOAT)
        os << " = " << convert_fpa_to_float(inst.angle);
#endif
        os << " )";
    }

    for (auto q_it = inst.q_begin(); q_it != inst.q_end(); q_it++)
        os << " " << *q_it;

    if (inst.number != INSTRUCTION::INVALID_NUMBER)
        os << " (" << inst.number << ")";

    return os;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION*
read_instruction_from_stream(generic_strm_type& istrm)
{
    io_encoding enc;
    _fill_or_consume_serialized_instruction(enc, istrm, generic_strm_read);

    INSTRUCTION::TYPE              type = static_cast<INSTRUCTION::TYPE>(enc.type_id);
    auto                           q_begin = std::begin(enc.qubits);
    auto                           q_end = q_begin + get_inst_qubit_count(type);
    fpa_type                       angle(std::begin(enc.angle), std::end(enc.angle));
    std::vector<INSTRUCTION::TYPE> urotseq(enc.urotseq_size);

    std::transform(enc.urotseq, enc.urotseq+enc.urotseq_size, urotseq.begin(),
                [] (auto t) { return static_cast<INSTRUCTION::TYPE>(t); });

    INSTRUCTION* inst = new INSTRUCTION{type, q_begin, q_end, angle, urotseq.begin(), urotseq.end()};
    return inst;
}

void
write_instruction_to_stream(generic_strm_type& ostrm, INSTRUCTION* inst)
{
    io_encoding enc;

    // type id
    enc.type_id = static_cast<uint8_t>(inst->type);

    // qubits
    std::copy(inst->q_begin(), inst->q_end(), std::begin(enc.qubits));

    // angle:
    auto words = inst->angle.get_words();
    std::move(words.begin(), words.end(), std::begin(enc.angle));

    // urotseq:
    enc.urotseq_size = inst->urotseq.size();
    std::transform(inst->urotseq.begin(), inst->urotseq.end(), std::begin(enc.urotseq),
                    [] (const auto t) { return static_cast<uint8_t>(t); });

    _fill_or_consume_serialized_instruction(enc, ostram, generic_strm_write);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* BEGINNING OF HELPER FUNCTIONS */

template <class IO_FUNCTION> void
_fill_or_consume_serialized_instruction(io_encoding& enc, generic_strm_type& strm, const IO_FUNCTION& io_fn)
{
    io_fn(strm, &enc.type_id, sizeof(enc.type_id));
    io_fn(strm, &enc.qubits, sizeof(qubit_type)*MAX_QUBITS);

    if (is_rotation_instruction(type))
    {
        // angle data
        io_fn(strm, &enc.fpa_word_count, sizeof(enc.fpa_word_count));
        assert(enc.fpa_word_count <= io_encoding::fpa_type::NUM_WORDS);
        io_fn(strm, enc.angle, sizeof(io_encoding::fpa_type::word_type) * enc.fpa_word_count);

        // rotation sequence
        io_fn(strm, &enc.urotseq_size, sizeof(enc.urotseq_size));
        assert(enc.urotseq_size <= io_encoding::UROTSEQ_CAPACITY);
        io_fn(strm, enc.urotseq, sizeof(uint8_t)*enc.urotseq_size);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
