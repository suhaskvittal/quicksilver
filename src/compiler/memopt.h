/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#ifndef COMPILER_MEMOPT_h
#define COMPILER_MEMOPT_h

#include "generic_io.h"
#include "instruction.h"
#include "compiler/memopt/impl.h"

#include <deque>
#include <limits>
#include <memory>
#include <unordered_set>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMOPT
{
public:
    using inst_ptr = INSTRUCTION*;
    using inst_window_type = std::deque<inst_ptr>;
    using ws_type = std::unordered_set<qubit_type>;
    using inst_array = std::vector<inst_ptr>;

    enum class EMIT_IMPL_ID
    {
        VISZLAI,
        HINT_SIMPLE,
        HINT_DISJOINT
    };

    constexpr static size_t PENDING_INST_BUFFER_SIZE{16384};
    constexpr static size_t OUTGOING_INST_BUFFER_SIZE{1024*1024};
    constexpr static size_t READ_LIMIT{2048};

    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_unrolled_inst_done{0};
    uint64_t s_memory_instructions_added{0};
    uint64_t s_memory_prefetches_added{0};
    uint64_t s_unused_bandwidth{0};
    uint64_t s_emission_calls{0};

    uint64_t s_total_rref{0};
    uint64_t s_num_rref{0};
    uint64_t s_timestep{0};

    std::array<uint64_t, 8> s_rref_histogram{};

    uint32_t num_qubits_;
    const size_t cmp_count_;
private:
    ws_type working_set_;

    // a buffer of instructions that need to be compiled (pending) or need to be written out (outgoing):
    inst_array pending_inst_buffer_;
    inst_array outgoing_inst_buffer_;

    // instruction windows for all qubits:
    std::unordered_map<qubit_type, inst_window_type> inst_windows_;

    // memory instruction emit implementation:
    std::unique_ptr<memopt::IMPL_BASE> emit_impl_;

    // for tracking re-references:
    std::unordered_map<qubit_type, uint64_t> last_rref_;
    std::unordered_map<qubit_type, inst_ptr> last_storing_inst_;

    const uint64_t print_progress_freq_;
public:
    MEMOPT(size_t cmp_count, EMIT_IMPL_ID, uint64_t print_progress_freq);

    void run(generic_strm_type& istrm, generic_strm_type& ostrm, 
                uint64_t stop_after_completing_n_instructions=std::numeric_limits<uint64_t>::max());
private:
    void read_instructions(generic_strm_type&);
    void drain_outgoing_buffer(generic_strm_type&, std::vector<inst_ptr>::iterator, std::vector<inst_ptr>::iterator);

    void emit_memory_instructions();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// could use a DAG, but this is simple and works
// note that this uses a `std::shared_ptr` since performance is non-critical
using inst_schedule_type = std::unordered_map<qubit_type, std::deque<std::shared_ptr<INSTRUCTION>>>;

bool validate_schedule(generic_strm_type& ground_truth, generic_strm_type& test, size_t cmp_count);

/*
 * Helper functions:
 * */

bool read_instructions(generic_strm_type&, inst_schedule_type&, size_t cmp_count, bool check_memory_access_validity);
bool compare_instruction_windows(const inst_schedule_type&, const inst_schedule_type&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif
