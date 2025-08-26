/*
    author: Suhas Vittal
    date:   2025 August 18
*/

#ifndef SIM_h
#define SIM_h

#include "instruction.h"
#include "sim/client.h"
#include "sim/meminfo.h"
#include "sim/routing.h"

#include <cstdint>
#include <random>
#include <unordered_map>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// External simulation variable for logical cycles 
// `GL_CYCLE` is for compute, `GL_MEMORY_CYCLES` is for memory (which is assumed to be slower)
extern uint64_t GL_CYCLE; 
extern uint64_t GL_MEMORY_CYCLES;
extern std::mt19937 GL_RNG;

class SIM
{
public:
    using inst_ptr = CLIENT::inst_ptr;

    struct CONFIG
    {
        uint64_t inst_warmup{100};
        uint64_t inst_sim{10'000};

        std::vector<std::string> client_trace_files;
        /*
            Timing parameters:
        */
        uint64_t compute_syndrome_extraction_time_ns{1200};  // 1.2us
        size_t   compute_rounds_per_cycle{27};
        /*
            organization of the compute memory:

            There will be `num_rows` rows, and each row will have `patches_per_row` patches.
            Each row is a two-wide row of patches. So for example, if `num_rows = 8` and
            `patches_per_row = 16`, the organization will be:
             
             | + . . . . . . . . |    "+" = junction, "." = bus, "P" = patch
             | . P P P P P P P P |
             | . P P P P P P P P |
             | + . . . . . . . . |

            And on the edges of the row (left, right, top, and bottom), there will be a bus.
            Upper and lower buses are shared with the previous and next row, respectively.

            Finally, at the top, we reserve an extra row for magic state pins, as such:
            _____________________
            | M M M M M M M M M |
            | + . . . . . . . . |
            | . P P P P P P P P |
            | . P P P P P P P P |
            | + . . . . . . . . |
        */
        size_t num_rows{8};
        size_t patches_per_row{16};
        /*
            Magic state factory config: by default, we have set 15 level 1 factories,
            and 1 level 2 factory.        
        */
        std::vector<size_t> num_15to1_factories_by_level{15,1};
    };

    struct clk_info
    {
        double clk_scale{1.0};
        double leap{0.0};

        clk_info() =default;
        clk_info(double freq_compute_khz, double freq_target_khz);

        // returns true if we should tick the target, whatever it is (i.e., memory or a factory)
        bool update_post_cpu_tick();
    };

    enum class EXEC_RESULT
    {
        SUCCESS,
        MEMORY_STALL,
        ROUTING_STALL,
        RESOURCE_STALL,
        // this is not a stall -- we need to wait for instructions to complete
        WAITING_FOR_QUBIT_TO_BE_READY
    }
private:
    std::vector<sim::CLIENT> clients_;
    
    // this is compute storage: can only perform operations on qubits in compute:
    std::vector<sim::PATCH> compute_;

    // magic state factories: magic states can be accessed from their respective patches:
    std::vector<sim::T_FACTORY> t_fact_;
    std::vector<clk_info> t_fact_clk_info_;

    // a buffer for accumulating all execution results each cycle:
    std::vector<EXEC_RESULT> exec_results_;

    /*
        Simulation parameters:
        - inst_warmup_: number of instructions to warm up the simulator. Don't need too many.
        - inst_sim_: number of instructions to simulate.
        - done_: is true once all `clients_` have completed `inst_sim_` instructions outside of warmup
    */
    // sim variables:
    const uint64_t inst_warmup_{100}; 
    const uint64_t inst_sim_{10'000};
    bool           done_{false};
    bool           warmup_{true};

    const double compute_speed_khz_;
    const size_t required_msfact_level_{1};  // indexed from 0, so 1 is a two-level factory
public:
    SIM(CONFIG=CONFIG{});
    ~SIM();

    void tick();

    bool is_done() const { return done_; }
    const std::vector<sim::CLIENT>& clients() const { return clients_; }
private:
    void init_t_state_factories(const CONFIG&);
    void init_routing_space(const CONFIG&);
    void init_compute(const CONFIG&);

    void client_try_retire(sim::CLIENT&);
    void client_try_execute(sim::CLIENT&);
    void client_try_fetch(sim::CLIENT&);

    EXEC_RESULT execute_instruction(sim::CLIENT&, inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif // SIM_h