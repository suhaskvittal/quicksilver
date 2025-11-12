/*
    author: Suhas Vittal
    date:   2025 September 17
*/

#ifndef SIM_MEMORY_h
#define SIM_MEMORY_h

#include "sim/operable.h"
#include "sim/client.h"
#include "sim/epr_generator.h"

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
    RETRY_MEMORY_ACCESS
};

struct MEMORY_EVENT_INFO
{
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

        bank_type() =default;
        bank_type(size_t capacity) : contents(capacity, QUBIT{-1,-1}) {}

        // returns the number of cycles required to reach location
        uint64_t rotate_to_location_and_store(std::vector<QUBIT>::iterator, QUBIT stored);
    };

    struct request_type
    {
        inst_ptr inst;
        QUBIT qubit;
        QUBIT victim;
        bool is_prefetch;
    };

    using search_result_type = std::tuple<bool, std::vector<bank_type>::iterator, std::vector<QUBIT>::iterator>;

    constexpr static uint64_t LOAD_CYCLES{2};
    constexpr static uint64_t STORE_CYCLES{1};
    constexpr static uint64_t MSWAP_CYCLES{LOAD_CYCLES + STORE_CYCLES};

    // set by `COMPUTE`
    ssize_t output_patch_idx_;

    // statistics:
    using client_stats_type = std::unordered_map<int8_t, uint64_t>;

    client_stats_type s_num_prefetch_requests{};
    client_stats_type s_num_prefetch_promoted_to_demand{};
    uint64_t s_memory_requests{0};
    uint64_t s_memory_prefetch_requests{0};
    uint64_t s_total_epr_buffer_occupancy_post_request{0};

    std::array<uint64_t, 8> s_epr_occu_histogram{};

    uint64_t s_decoupled_loads{0};
    uint64_t s_decoupled_stores{0};

    const size_t num_banks_;
    const size_t capacity_per_bank_;
    const bool   is_remote_module_;
protected:
    std::vector<bank_type> banks_;
    std::vector<request_type> request_buffer_;

    uint64_t cycle_free_{0};

    // EPR generator for remote modules (owned by this MEMORY_MODULE)
    EPR_GENERATOR* epr_generator_{nullptr};
public:
    MEMORY_MODULE(double freq_khz,
                    size_t num_banks,
                    size_t capacity_per_bank,
                    bool is_remote_module=false,
                    size_t epr_buffer_capacity=4,
                    uint64_t mean_epr_generation_cycle_time=10);

    ~MEMORY_MODULE();

    search_result_type find_qubit(QUBIT);
    search_result_type find_uninitialized_qubit();

    void initiate_memory_access(inst_ptr, QUBIT requested, QUBIT victim, bool is_prefetch=false);
    void dump_contents();

    bool can_serve_request() const;
    bool has_pending_store_for_qubit(QUBIT) const;

    // Access to EPR generator for COMPUTE (shared access, not ownership transfer)

    void OP_init() override;

    EPR_GENERATOR* get_epr_generator() const { return epr_generator_; }
    bool has_pending_requests() const { return !request_buffer_.empty(); }

    // Get all qubits stored in memory banks (for duplicate checking)
    std::vector<QUBIT> get_all_stored_qubits() const;
protected:
    void OP_handle_event(event_type) override;

    virtual bool serve_memory_request(const request_type&);

    std::vector<request_type>::iterator find_request_for_qubit(QUBIT);

    friend void mem_alloc_qubits_in_round_robin(std::vector<MEMORY_MODULE*>, std::vector<QUBIT>);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void mem_alloc_qubits_in_round_robin(std::vector<MEMORY_MODULE*>, std::vector<QUBIT>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif
