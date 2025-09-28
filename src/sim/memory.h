/*
    author: Suhas Vittal
    date:   2025 September 17
*/

#ifndef SIM_MEMORY_h
#define SIM_MEMORY_h

#include "sim/operable.h"
#include "sim/client.h"

#include <deque>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class MEMORY_EVENT_TYPE
{
    MEMORY_ACCESS_COMPLETED,
    COMPUTE_COMPLETED_INST,
    
    // only when `is_remote_module_` is true
    REMOTE_EPR_PAIR_GENERATED 
};

struct MEMORY_EVENT_INFO
{
    // the bank that has completed the request (for `MEMORY_ACCESS_COMPLETED` events)
    size_t bank_with_completed_request{};
};

/*
    This is a group of logical memory blocks (i.e., QLDPC code blocks).

    A single block is represented by a memory "bank". Only one request
    can be served per cycle, but each bank can be accessed independently.
    So, for example, bank/block 0 can be accessed while bank/block 1 is
    serving a request from a prior cycle.

    All banks must be of the same code block. This is a simulator simplification,
    but also a likely hardware constraint on fixed connectivity hardware like
    superconducting qubits, as different QEC codes require different connectivity.
*/
class MEMORY_MODULE : public OPERABLE<MEMORY_EVENT_TYPE, MEMORY_EVENT_INFO>
{
public:
    using typename OPERABLE<MEMORY_EVENT_TYPE, MEMORY_EVENT_INFO>::event_type;

    using inst_ptr = CLIENT::inst_ptr;

    struct bank_type
    {
        std::vector<QUBIT> contents;
        uint64_t cycle_free{0};

        bank_type() =default;
        bank_type(size_t capacity) : contents(capacity, QUBIT{-1,-1}) {}
    };

    struct request_type
    {
        inst_ptr inst;
        QUBIT qubit;
        bool is_prefetch;

        std::optional<QUBIT> victim{std::nullopt};  // may not be known if requests are made by the simulator
    };

    using search_result_type = std::tuple<bool, std::vector<bank_type>::iterator, std::vector<QUBIT>::iterator>;

    // set by `COMPUTE`
    ssize_t output_patch_idx_;

    // statistics:
    using client_stats_type = std::unordered_map<int8_t, uint64_t>;

    client_stats_type s_num_prefetch_requests_{};
    client_stats_type s_num_prefetch_promoted_to_demand_{};
    uint64_t s_memory_requests_{0};
    uint64_t s_memory_prefetch_requests_{0};
    uint64_t s_total_epr_buffer_occupancy_post_request_{0};

    const size_t num_banks_;
    const size_t capacity_per_bank_;
    const bool   is_remote_module_;
    const size_t   epr_buffer_capacity_;
    const uint64_t mean_epr_generation_cycle_time_;
protected:
    std::vector<bank_type> banks_;
    std::vector<request_type> request_buffer_;

    size_t epr_buffer_occu_{0};
public:
    MEMORY_MODULE(double freq_khz, 
                    size_t num_banks, 
                    size_t capacity_per_bank,
                    bool is_remote_module=false,
                    size_t epr_buffer_capacity=4,
                    uint64_t mean_epr_generation_cycle_time=10);

    search_result_type find_qubit(QUBIT);
    search_result_type find_uninitialized_qubit();

    void initiate_memory_access(inst_ptr, QUBIT, bool is_prefetch=false);
    // returns true if the request was immediately served, false otherwise
    bool serve_mswap(inst_ptr, QUBIT requested, QUBIT victim);

    void dump_contents();

    void OP_init() override;
protected:
    void OP_handle_event(event_type) override;

    virtual bool serve_memory_request(const request_type&);

    std::vector<request_type>::iterator find_request_for_qubit(QUBIT);
    std::vector<request_type>::iterator find_request_for_bank(size_t, std::vector<request_type>::iterator);

    friend void mem_alloc_qubits_in_round_robin(std::vector<MEMORY_MODULE*>, std::vector<QUBIT>);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void mem_alloc_qubits_in_round_robin(std::vector<MEMORY_MODULE*>, std::vector<QUBIT>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif