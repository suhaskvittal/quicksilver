/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#ifndef SIM_PRODUCTION_h
#define SIM_PRODUCTION_h

#include "globals.h"
#include "sim/operable.h"

#include <deque>
#include <optional>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class ResourceType { T, CCZ, EPR };
    
/*
 * Represents one unit of production for the specified
 * `resource`.
 * */
struct ProducerQuantum
{
    ResourceType resource;

    /*
     * Distillation data (un-needed for cultivation quantum)
     * */
    bool is_distiller;
    size_t d_input_states,
           d_output_states;

    size_t buffer_size{1};

    /*
     * Resource estimation metadata:
     * */
    double infidelity;
    size_t physical_qubit_count;
    cycle_type cycles_per_production;
    // Idle cycles between completing one batch and starting the next.
    cycle_type restart_delay{1};
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Bulk production object that manages creation of resource states.
 *
 * This takes away a lot of resolution from implementing magic states (i.e., distillation
 * protocols can be pipelined as all states are not consumed at once), but this is faster
 * for simulation.
 *
 * Plus, if that level of fidelity is really necessary, write a custom `ProducerQuantum` that
 * models it.
 * */
class BulkProducer : public Operable
{
public:
    struct level_type
    {
        struct event_type
        {
            size_t prod_id;
            size_t count;
            cycle_type ready_cycle;
        };

        ProducerQuantum traits;
        size_t          producer_count,
                        unified_buffer_occupancy,
                        unified_buffer_capacity;

        // one entry per `producer_count`
        std::vector<cycle_type> next_available_cycle,   // availabiliity cycle
                                preq_resource_count;    // number of resources given to the producer

        // essentially a glorified event queue
        std::deque<event_type> event_queue;

        /*
         * Statistics:
         * */
        uint64_t s_completions{0};
        uint64_t s_cycles_stalled_on_input{0};
        uint64_t s_cycles_stalled_on_buffer{0};

        /*
         * Constructor: quantum and count
         * */
        level_type(ProducerQuantum, size_t);

        /*
         * Adds an event to `event_queue` and updates other structures
         * */
        void add_event(size_t producer_id, size_t count, cycle_type ready_cycle);

        /*
         * Checks if the head of `event_queue` is at or earlier than `current_cycle`. If so, returns true
         * and updates data structures.
         * */
        bool pop_event(cycle_type current_cycle);

        size_t occupancy_remaining() const { return unified_buffer_capacity - unified_buffer_occupancy; }
    };
private:
    /*
     * A higher level is further into `levels_` . So `levels_.back()` contains
     * highest fidelity resource states.
     * */
    std::vector<level_type> levels_;
public:
    BulkProducer(std::string_view name, double freq_khz);

    long operate() override;

    void add_level(ProducerQuantum, size_t);

    /*
     * Drains `count` resource states from the top-level buffer. Returns false (and does
     * nothing) if fewer than `count` are available.
     * */
    bool consume(size_t count);

    size_t top_level_count() const { return levels_.back().producer_count; }
    size_t top_level_buffer_occupancy() const { return levels_.back().unified_buffer_occupancy; }
    size_t top_level_buffer_capacity() const { return levels_.back().unified_buffer_capacity; }
    double top_level_infidelity() const { return levels_.back().traits.infidelity; }
    ProducerQuantum level_traits(size_t i) const { return levels_[i].traits; }
    ProducerQuantum top_level_traits() { return levels_.back().traits; }
    const level_type& level(size_t i) const { return levels_[i]; }
    const level_type& top_level() const { return levels_.back(); }
    size_t level_count() const { return levels_.size(); }

    /*
     * Total physical qubit footprint across all levels (excludes buffer overheads, which
     * are accounted for separately during allocation).
     * */
    size_t physical_qubit_count() const;

    /*
     * True if the top-level buffer is full -- i.e., production cannot make any further
     * progress until the buffer is drained by a consumer.
     * */
    bool is_saturated() const { return top_level_buffer_occupancy() >= top_level_buffer_capacity(); }

    /*
     * Earliest cycle at which any level has a pending event, if any. Used to compute
     * skip-cycle targets.
     * */
    std::optional<cycle_type> next_event_cycle() const;

    void print_deadlock_info(std::ostream&) const override;
private:
    long handle_resource_completions(level_type&, ssize_t level_idx);
    long handle_resource_initiations(level_type&, ssize_t level_idx);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

cycle_type retry_adjusted_latency(cycle_type, size_t n_inputs, double e_input);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Pre-defined `ProducerQuantum` objects. for T production
 * */

ProducerQuantum t_cc_cultivation(size_t d);
ProducerQuantum t_15to1_distillation(size_t dx, size_t dz, size_t dm, double input_state_infidelity);


/*
 * Pre-defined `ProducerQuantum objects` for EPR production
 * */
enum class EDImplMethod 
{ 
    BARE,
    YOKED_SURFACE_CODE,
    BIVARIATE_BICYCLE_CODE,

    /*
     * Disaggregated is where ED is implemented on two systems with very
     * different latencies. For purposes of timing, we assume the time taken
     * by the compute substrate is much lower than the memory substrate (which
     * should always be true.) But, the compute substrate matters for physical
     * qubit estimates.
     * */
    DISAGGREGATED_TYPE_A   // YSC on compute qubits, BB code on memory substrate
};

// Perfect EPR pairs, one every 4*d memory cycles, with an eight-pair buffer.
ProducerQuantum ideal_epr_distribution(size_t memory_code_distance);

/*
 * Protocol 1:
 *  [[14,6,4]] into [[32,25,3]]
 *
 *  First level infidelity: 4.4e-6
 *  Second level infidelity: 6.1e-13
 * */

ProducerQuantum ed_protocol_1a(EDImplMethod);
ProducerQuantum ed_protocol_1b(EDImplMethod);

/*
 * Protocol 2:
 *  [[4,2,2]] into [[27,18,4]]
 *
 *  First level infidelity: 4.9e-4
 *  Second level infidelity: 4.3e-10
 * */

ProducerQuantum ed_protocol_2a(EDImplMethod);
ProducerQuantum ed_protocol_2b(EDImplMethod);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif  // SIM_PRODUCTION_h
