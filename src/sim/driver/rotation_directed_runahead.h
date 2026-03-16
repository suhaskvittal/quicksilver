/*
 *  author: Suhas Vittal
 *  date:   12 March 2026
 * */

#ifndef SIM_DRIVER_ROTATION_DIRECTED_RUNAHEAD_h
#define SIM_DRIVER_ROTATION_DIRECTED_RUNAHEAD_h

#include "instruction.h"
#include "sim/compute_subsystem.h"

#include <limits>
#include <queue>
#include <unordered_set>

namespace sim
{
namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class ROTATION_DIRECTED_RUNAHEAD
{
public:
    using inst_ptr = INSTRUCTION*;

    /*
     * A pending rotation request:
     * */
    struct request_type
    {
        inst_ptr inst;
        size_t   dag_layer;
        QUBIT*   pinned_qubit{nullptr};
        bool     done{false};
        bool     invalidated{false};
        bool     critical{false};

        /*
         * Debug info:
         * */
        INSTRUCTION triggering_inst;

        cycle_type cycle_start{std::numeric_limits<cycle_type>::max()};
        cycle_type cycle_done;
    };

    using request_table_type = std::unordered_map<inst_ptr, request_type*>;

    /*
     * Comparator for priority queue: oldest entry (smallest dag_layer,
     * then smallest instruction number) has highest priority.
     * Note: std::priority_queue is a max-heap, so we use > for min-heap behavior.
     * */
    struct request_priority_comparator
    {
        bool operator()(const request_type* a, const request_type* b) const;
    };

    using pending_queue_type = std::priority_queue<request_type*,
                                                    std::vector<request_type*>,
                                                    request_priority_comparator>;

    /*
     * Statistics:
     * */

    uint64_t s_invalidated_in_queue{0};
    uint64_t s_requests{0};
    uint64_t s_requests_completed{0};
    uint64_t s_requests_invalidated{0};

    uint64_t s_completion_buffer_occupancy_sum{0};
    uint64_t s_completion_buffer_occupancy_ticks{0};
private:
    request_table_type request_table_;
    std::unordered_set<inst_ptr> completion_buffer_;

    std::vector<QUBIT*> free_qubits_;

    /*
     * Priority queue for rotation requests without an allocated qubit.
     * Ordered by (dag_layer, instruction number) so oldest entries are served first.
     * When a request with a qubit completes, the top of this queue
     * (skipping invalidated entries) receives the freed qubit.
     * */
    pending_queue_type pending_queue_;

    COMPUTE_SUBSYSTEM* compute_subsystem_;
public:
    ROTATION_DIRECTED_RUNAHEAD(COMPUTE_SUBSYSTEM*);

    long execute();

    /*
     * Submits a rotation request and returns true if the request was added.
     * If a free qubit is available, the request receives it immediately;
     * otherwise, the request goes into pending_queue_.
     * */
    bool submit_request(inst_ptr, size_t dag_layer, inst_ptr triggering_inst);

    request_type* find_request_and_only_return_if_complete(inst_ptr);
    void          delete_request(inst_ptr);

    /*
     * Returns true if the rotation instruction is already pending
     * */
    bool is_request_pending(inst_ptr) const;

    /*
     * Returns number of uops retired for the given rotation
     * */
    size_t get_request_progress(inst_ptr) const;

    /*
     * Indicates that the given instruction is at the head of the DAG and needs to
     * be completed ASAP.
     * */
    void mark_request_as_critical(inst_ptr);

    /*
     * Invalidates the rotation entry for the given instruction, and deletes it if it is currently
     * in progress.
     * */
    void invalidate_request(inst_ptr);
private:
    void delete_request(request_type*);

    /*
     * Pops from pending_queue_, deleting any invalidated entries,
     * and returns the first valid request (or nullptr if none).
     * */
    request_type* pop_next_valid_pending_request();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace driver
} // namespace sim


#endif // SIM_DRIVER_ROTATION_DIRECTED_RUNAHEAD_h
