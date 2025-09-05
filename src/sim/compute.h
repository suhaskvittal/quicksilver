/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#ifndef SIM_COMPUTE_h
#define SIM_COMPUTE_h

#include "instruction.h"
#include "sim/client.h"
#include "sim/clock.h"
#include "sim/meminfo.h"
#include "sim/magic_state.h"

#include <cstdint>
#include <memory>
#include <random>
#include <optional>
#include <utility>
#include <vector>

extern std::mt19937 GL_RNG;

//#define QS_SIM_DEBUG

namespace sim
{

class MEMORY_MODULE;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    organization of the compute memory:

    There will be `num_rows` rows, and each row will have `patches_per_row` patches.
    Each row is a two-wide row of patches. So for example, if `num_rows = 8` and
    `patches_per_row = 16`, the organization will be:
        
        | + . . . . . . . . + |    "+" = junction, "." = bus, "P" = patch
        | . P P P P P P P P . |
        | . P P P P P P P P . |
        | + . . . . . . . . + |

    And on the edges of the row (left, right, top, and bottom), there will be a bus.
    Upper and lower buses are shared with the previous and next row, respectively.

    Finally, at the top, we reserve an extra row for magic state pins, as such:
    ______________________ 
    | M M M M M M M M M M |
    | + . . . . . . . . + |
    | . P P P P P P P P . |
    | . P P P P P P P P . |
    | + . . . . . . . . + |

    And at the bottom, we reserve an extra row for memory pins, as such:

    | + . . . . . . . . + |
    | . P P P P P P P P . |
    | . P P P P P P P P . |
    | + . . . . . . . . + |
    | M M M M M M M M M M |
    ______________________ 
*/

class COMPUTE : public CLOCKABLE
{
public:
    using inst_ptr = INSTRUCTION*;
    using client_ptr = std::unique_ptr<CLIENT>;

    struct CONFIG
    {
        // `clients_` are initialized from the trace files:
        std::vector<std::string> client_trace_files;

        // compute dimensions (see above for organization)
        // We want `num_rows * patches_per_row > program_memory_size`
        size_t num_rows{16};
        size_t patches_per_row{16};

        // compute parameters:
        size_t target_t_fact_level{1};
    };

    enum class EXEC_RESULT
    {
        SUCCESS,
        MEMORY_STALL,
        ROUTING_STALL,
        RESOURCE_STALL,
        WAITING_FOR_QUBIT_TO_BE_READY
    };
private:
    // a pointer to each workload running on the system
    std::vector<client_ptr>  clients_;
    
    // compute storage:
    //    factory pins are from `0` to `patch_idx_compute_start_` (not including `patch_idx_compute_start_`)
    //    program qubits are from `patch_idx_compute_start_` to `patch_idx_memory_start_` (not including `patch_idx_memory_start_`)
    //    memory pins are from `patch_idx_memory_start_` to `patches_.size()` (not including `patches_.size()`)
    std::vector<PATCH>       patches_;
    size_t                   patch_idx_compute_start_{0};
    size_t                   patch_idx_memory_start_{0};

    // a vector of pointers to magic state factories:
    std::vector<T_FACTORY*> t_fact_;

    // a vector of pointers to memory modules:
    std::vector<MEMORY_MODULE*> memory_;

    // a buffer for accumulating all execution results each cycle:
    std::vector<EXEC_RESULT> exec_results_;

    // we will only use T magic states from factories of this level:
    const size_t target_t_fact_level_{1};
public:
    COMPUTE(double freq_ghz, CONFIG, const std::vector<T_FACTORY*>&, const std::vector<MEMORY_MODULE*>&);

    void operate() override;
    
    // returns true if the routing space was allocated, false otherwise
    bool alloc_routing_space(const PATCH& from, const PATCH& to, uint64_t block_endpoints_for, uint64_t block_path_for);

    // Returns a pointer to the victim qubit. We return a pointer 
    // as this qubit resides in one of the clients' qubit arrays.
    CLIENT::qubit_info_type* select_victim_qubit();
    
    // Use this if we fail to find a victim qubit:
    CLIENT::qubit_info_type* select_random_victim_qubit(int8_t incoming_client_id, qubit_type incoming_qubit_id);

    const std::vector<client_ptr>& clients() const { return clients_; }
    uint64_t current_cycle() const { return cycle_; }
private:
    using bus_array = std::vector<ROUTING_BASE::ptr_type>;
    using bus_info = std::pair<bus_array, bus_array>;

    void init_clients(const CONFIG&);
    bus_info init_routing_space(const CONFIG&);
    void init_compute(const CONFIG&, const bus_array& junctions, const bus_array& buses);

    void client_try_retire(client_ptr&);
    void client_try_execute(client_ptr&);
    void client_try_fetch(client_ptr&);
    
    void issue_memory_swap_request(client_ptr&, qubit_type);

    EXEC_RESULT execute_instruction(client_ptr&, inst_ptr);

    PATCH::bus_array::const_iterator find_free_bus(const PATCH& p) const;
    
    // this attempts to estimate how close an instruction is to being ready to execute
    // it is computed by determining how deep the instruction is in one of its arguments' windows.
    size_t compute_instruction_timeliness(const inst_ptr&) const;

    std::vector<PATCH>::iterator          find_patch_containing_qubit(int8_t client_id, qubit_type qubit_id);
    std::vector<MEMORY_MODULE*>::iterator find_memory_module_containing_qubit(int8_t client_id, qubit_type qubit_id);

    friend class MEMORY_MODULE;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool is_software_instruction(INSTRUCTION::TYPE);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif