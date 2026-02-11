/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#ifndef SIM_ROTATION_SUBSYSTEM_h
#define SIM_ROTATION_SUBSYSTEM_h

#include "sim/compute_base.h"

#include <limits>
#include <queue>
#include <unordered_set>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class ROTATION_SUBSYSTEM : public COMPUTE_BASE
{
public:
    using inst_ptr = COMPUTE_BASE::inst_ptr;
    using execute_result_type = COMPUTE_BASE::execute_result_type;

    /*
     * A pending rotation request.
     * */
    struct rotation_request_entry
    {
        inst_ptr inst;
        size_t   dag_layer;
        QUBIT*   allocated_qubit{nullptr};
        bool     done{false};

        bool invalidated{false};

        /*
         * Debug info
         * */
        std::string triggering_inst_info;

        /*
         * These are compute cycles, not cycles of the `ROTATION_SUBSYSTEM` (see `parent_` field)
         * */
        cycle_type cycle_installed{std::numeric_limits<cycle_type>::max()};
        cycle_type cycle_done;
    };

    using request_map_type = std::unordered_map<inst_ptr, rotation_request_entry*>;

    /*
     * Comparator for priority queue: oldest entry (smallest dag_layer,
     * then smallest instruction number) has highest priority.
     * Note: std::priority_queue is a max-heap, so we use > for min-heap behavior.
     * */
    struct request_priority_comparator
    {
        bool operator()(const rotation_request_entry* a, const rotation_request_entry* b) const;
    };

    using pending_queue_type = std::priority_queue<rotation_request_entry*,
                                                    std::vector<rotation_request_entry*>,
                                                    request_priority_comparator>;

    uint64_t s_rotations_completed{0};
    uint64_t s_rotation_service_cycles{0};
    uint64_t s_rotation_idle_cycles{0};
    uint64_t s_invalidates{0};
private:
    request_map_type request_map_;

    /*
     * Qubits available for serving rotation requests.
     *
     * `active_qubit_` is an in-use qubit that is only qubit that
     * accepts operations.
     * */
    std::vector<QUBIT*> free_qubits_;
    QUBIT*              active_qubit_{nullptr};

    /*
     * Priority queue for rotation requests without an allocated qubit.
     * Ordered by (dag_layer, instruction number) so oldest entries are served first.
     * When a request with a qubit completes, the top of this queue
     * (skipping invalidated entries) receives the freed qubit.
     * */
    pending_queue_type pending_queue_;

    /*
     * This limits the amount of bandwidth that this object can
     * consume.
     * */
    double watermark_;

    /*
     * We need this private variable to limit T execution once
     * the number of magic states falls below 
     *      `watermark_ * total_magic_state_count_at_cycle_start_`
     * (or reaches 1)
     * */
    size_t total_magic_state_count_at_cycle_start_;

    /*
     * `parent_` is mostly used to update stats.
     * */
    const COMPUTE_SUBSYSTEM* parent_;
public:
    ROTATION_SUBSYSTEM(double freq_khz, 
                        size_t capacity,
                        COMPUTE_SUBSYSTEM* parent,
                        double watermark);
    ~ROTATION_SUBSYSTEM();

    void print_deadlock_info(std::ostream&) const override;

    /*
     * Returns true if a rotation request can be allocated a
     * qubit.
     * */
    bool can_accept_request() const;

    /*
     * Submits a rotation request and returns true if the request was added.
     * If a free qubit is available, the request receives it immediately;
     * otherwise, the request goes into pending_queue_.
     * */
    bool submit_request(inst_ptr, size_t dag_layer, inst_ptr triggering_inst);

    /*
     * Returns true if the rotation instruction is already pending
     * */
    bool is_request_pending(inst_ptr) const;

    /*
     * Returns true if the request for the given rotation instruction is complete on some given qubit.
     * */
    bool find_and_delete_request_if_done(inst_ptr);

    /*
     * Returns number of uops retired for the given rotation
     * */
    size_t get_progress(inst_ptr) const;

    /*
     * Invalidates the rotation entry for the given instruction, and deletes it if it is currently
     * in progress.
     * */
    void invalidate(inst_ptr);
protected:
    long operate() override;
private:
    void delete_request(rotation_request_entry*);
    void get_new_active_qubit();

    /*
     * Pops from pending_queue_, deleting any invalidated entries,
     * and returns the first valid request (or nullptr if none).
     * */
    rotation_request_entry* pop_next_valid_pending_request();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_ROTATION_SUBSYSTEM_h
