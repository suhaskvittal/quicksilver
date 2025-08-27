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
#include <utility>
#include <vector>

extern std::mt19937 GL_RNG;

//#define QS_SIM_DEBUG

namespace sim
{

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
    std::vector<PATCH>       patches_;
    size_t                   patches_reserved_for_resource_pins_{0};

    // a vector pointers to magic state factories:
    std::vector<T_FACTORY*> t_fact_;

    // a buffer for accumulating all execution results each cycle:
    std::vector<EXEC_RESULT> exec_results_;

    // we will only use T magic states from factories of this level:
    const size_t target_t_fact_level_{1};
public:
    COMPUTE(uint64_t t_sext_round_ns, size_t code_distance, CONFIG, const std::vector<T_FACTORY*>&);

    void operate() override;

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

    EXEC_RESULT execute_instruction(client_ptr&, inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool is_software_instruction(INSTRUCTION::TYPE);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif