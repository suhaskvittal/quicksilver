/*
    author: Suhas Vittal
    date:   25 August 2025
*/

#ifndef SIM_CLIENT_h
#define SIM_CLIENT_h

#include "instruction.h"

#include <cstdint>
#include <unordered_map>
#include <variant>
#include <vector>

#include <zlib.h>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct CLIENT
{
    using inst_ptr = INSTRUCTION*;
    using inst_window_type = std::deque<inst_ptr>;
    using win_array_type = std::unordered_map<qubit_type, inst_window_type>;
    using strm_variant_type = std::variant<std::ifstream, gzFile>;

    enum class TRACE_FILE_TYPE { BINARY, GZ };

    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_cycles_stalled{0};
    uint64_t s_cycles_stalled_by_contention{0};
    uint64_t s_cycles_stalled_by_mem{0};
    uint64_t s_cycles_stalled_by_magic_states{0};

    win_array_type qubit_inst_windows;

    std::string       trace_file;
    TRACE_FILE_TYPE   trace_file_type;
    strm_variant_type trace_istrm;

    CLIENT(std::string trace_file);
    ~CLIENT();

    INSTRUCTION read_instruction_from_trace();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif