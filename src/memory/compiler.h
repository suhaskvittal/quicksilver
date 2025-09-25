/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#ifndef MEMORY_COMPILER_h
#define MEMORY_COMPILER_h

#include "generic_io.h"
#include "instruction.h"

#include <deque>
#include <limits>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY_COMPILER
{
public:
    using inst_ptr = INSTRUCTION*;
    using inst_window_type = std::deque<inst_ptr>;

    constexpr static size_t PENDING_INST_BUFFER_SIZE{16384};
    constexpr static size_t OUTGOING_INST_BUFFER_SIZE{16384};
    constexpr static size_t READ_LIMIT{2048};

    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_memory_instructions_added{0};
    uint64_t s_memory_prefetches_added{0};

    const size_t cmp_count_;
    const bool   using_rotation_directed_prefetch_;
private:
    // compute qubits:
    std::vector<qubit_type> qubits_in_cmp_;
    std::vector<size_t>     qubit_use_count_{};

    // a buffer of instructions that need to be compiled (pending) or need to be written out (outgoing):
    std::vector<inst_ptr> pending_inst_buffer_;
    std::vector<inst_ptr> outgoing_inst_buffer_;

    // instruction windows for all qubits:
    std::unordered_map<qubit_type, inst_window_type> inst_windows_;

    uint32_t num_qubits_;

    const uint64_t print_progress_freq_;
public:
    MEMORY_COMPILER(size_t cmp_count, bool enable_rotation_directed_prefetch, uint64_t print_progress_freq);

    void run(generic_strm_type& istrm, generic_strm_type& ostrm, uint64_t stop_after_completing_n_instructions=std::numeric_limits<uint64_t>::max());
private:
    void read_instructions(generic_strm_type&);
    void drain_outgoing_buffer(generic_strm_type&, std::vector<inst_ptr>::iterator, std::vector<inst_ptr>::iterator);

    void emit_memory_instructions();
    
    // helper functions for managing victim selection:
    ssize_t compute_victim_index(qubit_type, const std::vector<double>& qubit_scores, const std::vector<qubit_type>& do_not_evict);
    void remove_useless_memory_instructions_to_qubit(qubit_type);
    
    // returns the number of prefetches performed
    size_t do_rotation_directed_prefetch(inst_ptr, size_t pf_bandwidth, const std::vector<double>& qubit_scores);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif