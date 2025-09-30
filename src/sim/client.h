/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#ifndef SIM_CLIENT_h
#define SIM_CLIENT_h

#include "instruction.h"
#include "generic_io.h"

#include <iosfwd>
#include <string>
#include <variant>
#include <vector>

#include <zlib.h>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct QUBIT
{
    int8_t     client_id;
    qubit_type qubit_id;

    bool operator==(const QUBIT& other) const;
    std::string to_string() const;
};

std::ostream& operator<<(std::ostream&, const QUBIT&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct CLIENT
{
    using inst_ptr = INSTRUCTION*;

    constexpr static size_t GEN_IO_BIN_IDX{0},
                            GEN_IO_GZ_IDX{1};

    const std::string trace_file;
    generic_strm_type trace_istrm;
    bool              has_hit_eof_once{false};

    const int8_t id;
    const size_t num_qubits;
    
    // statistics:
    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_unrolled_inst_done{0};
    uint64_t s_inst_routing_stall_cycles{0};
    uint64_t s_inst_resource_stall_cycles{0};
    uint64_t s_inst_memory_stall_cycles{0};

    uint64_t s_mswap_count{0};
    uint64_t s_mprefetch_count{0};

    uint64_t s_t_gate_count{0};
    double   s_total_t_error{0};

    // functions and constructor:
    CLIENT(std::string trace_file, int8_t id);
    ~CLIENT();

    inst_ptr read_instruction_from_trace();
    bool eof() const;
    size_t open_file();
    void   close_file();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// specialization of `QUBIT` for `std::hash`:
namespace std
{

template <>
struct hash<sim::QUBIT>
{
    size_t 
    operator()(const sim::QUBIT& q) const
    {
        return std::hash<int8_t>()(q.client_id) ^ std::hash<qubit_type>()(q.qubit_id);
    }
};

}  // namespace std

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // SIM_CLIENT_h