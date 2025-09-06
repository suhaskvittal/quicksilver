/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef TYPES_h
#define TYPES_h

#include "fixed_point/angle.h"

#include <cstdint>
#include <iosfwd>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using qubit_type = int64_t;

constexpr std::string_view BASIS_GATES[] = 
{
    "h", "x", "y", "z", 
    "s", "sx", "sdg", "sxdg",
    "t", "tx", "tdg", "txdg",
    "cx", "cz", "swap",
    "rx", "rz",
    "ccx", "ccz",
    "mz", "mx",
    "nil"
};

struct INSTRUCTION
{
    constexpr static size_t FPA_PRECISION = 2048;

    using fpa_type = FPA_TYPE<FPA_PRECISION>;

    // `io_encoding` is to simplify the serialization of the instruction to a byte stream.
    struct io_encoding
    {
        using gate_id_type = uint8_t;

        gate_id_type        type_id{static_cast<gate_id_type>(INSTRUCTION::TYPE::NIL)};
        qubit_type          qubits[3]{-1,-1,-1};                   // all gates are at most 3-qubit gates
        uint16_t            fpa_word_count{fpa_type::NUM_WORDS};   // need this for compatibility in case `FPA_PRECISION` changes.
        fpa_type::word_type angle[fpa_type::NUM_WORDS];
        uint32_t            urotseq_size{0};
        gate_id_type*       urotseq{nullptr};

        io_encoding() =default;
        io_encoding(const INSTRUCTION*);
        ~io_encoding();

        // `read_write` requires the function argument to require
        //  (1) a pointer to a memory location to read from/write to, and (2) the size of the memory location.
        // We have a single function since the calling code is the same for both reading and writing.
        // We use templates to abstract away the IO functions. So, zlib, lzma, or stdio work equally well.
        template <class RW_FUNC> void read_write(const RW_FUNC&);
    };

    enum class TYPE
    {
         // supported quantum gates:
        H, X, Y, Z, 
        S, SX, SDG, SXDG, 
        T, TX, TDG, TXDG, 
        CX, CZ, SWAP,
        RX, RZ, 
        CCX, CCZ, 
        MZ, MX,
        NIL
    };

    TYPE type;
    
    std::vector<qubit_type> qubits;   // used by all quantum gates
    fpa_type                angle{};
    std::vector<TYPE>       urotseq{}; // "unrolled rotation sequence" of Clifford+T gates that implement `angle`
    /*
        Statistics (only for simulation):
    */
    uint64_t s_time_at_head_of_window{std::numeric_limits<uint64_t>::max()};
    uint64_t s_time_completed{std::numeric_limits<uint64_t>::max()};
    /*
        Simulation variables:
    */
    uint64_t inst_number{};
    bool     is_running{false};
    uint64_t cycle_done{std::numeric_limits<uint64_t>::max()};

    // gates like RZ/RX require multiple sub-operations to complete, so `uop_completed` 
    // is used to track the progress of the instruction.
    INSTRUCTION* curr_uop{nullptr};
    size_t   uop_completed{0};
    size_t   num_uops{0};

    INSTRUCTION(TYPE, std::vector<qubit_type>);
    INSTRUCTION(io_encoding&&);
    INSTRUCTION(const INSTRUCTION&) =default;

    // we require that the rotations are provided via iterators instead of an `initializer_list` 
    // since the sequence can be rather long.
    template <class ITER_TYPE> INSTRUCTION(TYPE, 
                                            std::vector<qubit_type>, 
                                            fpa_type, 
                                            ITER_TYPE urotseq_begin, 
                                            ITER_TYPE urotseq_end);

    io_encoding serialize() const;

    std::string to_string() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::ostream& operator<<(std::ostream&, const INSTRUCTION&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class RW_FUNC> void
INSTRUCTION::io_encoding::read_write(const RW_FUNC& rwf)
{
    rwf((void*)&type_id, sizeof(type_id));
    rwf((void*)qubits, 3*sizeof(qubit_type));

    // fixed point angle:
    rwf((void*)&fpa_word_count, sizeof(fpa_word_count));
    rwf((void*)angle, fpa_word_count * sizeof(fpa_type::word_type));

    // unrolled rotation sequence:
    rwf((void*)&urotseq_size, sizeof(urotseq_size));
    if (urotseq_size > 0)
    {
        if (urotseq == nullptr)
            urotseq = new gate_id_type[urotseq_size];
        rwf((void*)urotseq, urotseq_size * sizeof(gate_id_type));
    }
}

template <class ITER_TYPE>
INSTRUCTION::INSTRUCTION(TYPE _type, 
                         std::vector<qubit_type> _qubits, 
                         fpa_type _angle, 
                         ITER_TYPE urotseq_begin, 
                         ITER_TYPE urotseq_end)
    :type{_type},
    qubits(_qubits),
    angle(_angle),
    urotseq(urotseq_begin, urotseq_end)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif