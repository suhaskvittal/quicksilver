/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#ifndef SIM_ROTATION_SUBSYSTEM_h
#define SIM_ROTATION_SUBSYSTEM_h

#include "sim/compute_base.h"

#include <limits>

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

        bool                    invalidated{false};
        rotation_request_entry* next_request{nullptr};

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
     * Batch request submission (see `submit_batch_request()`)
     * */
    struct batch_request_info
    {
        inst_ptr inst;
        size_t   dag_layer;
    };

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
     * `submit_rotation_request` submits a standalone request and returns true
     * if the request was added.
     *
     * `submit_batch_request` submits a group of requests which form a linked list.
     * Once the head of the linked list is retrieved, it is removed and the 
     * next entry starts immediately.
     * */
    bool submit_request(inst_ptr, size_t dag_layer, inst_ptr triggering_inst);
    bool submit_batch_request(std::vector<batch_request_info>, inst_ptr triggering_inst);

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
    /*
     * Returns true if this is the last entry in the linked list
     * */
    bool delete_request(rotation_request_entry*);
    void get_new_active_qubit();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_ROTATION_SUBSYSTEM_h
