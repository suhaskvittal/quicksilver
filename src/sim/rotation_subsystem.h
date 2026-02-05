/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#ifndef SIM_ROTATION_SUBSYSTEM_h
#define SIM_ROTATION_SUBSYSTEM_h

#include "sim/compute_base.h"

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
        QUBIT* allocated_qubit{nullptr};
        bool done{false};

        /*
         * These are compute cycles, not cycles of the `ROTATION_SUBSYSTEM`
         * */
        cycle_type cycle_installed;
        cycle_type cycle_done;
    };

    uint64_t s_rotations_completed{0};
    uint64_t s_rotation_service_cycles{0};
    uint64_t s_rotation_idle_cycles{0};
    uint64_t s_invalidates{0};
private:
    /*
     * Stores assignment of rotation gates to logical qubits.
     * */
    std::unordered_map<inst_ptr, rotation_request_entry> rotation_assignment_map_;

    /*
     * Qubits available for serving rotation requests.
     * */
    std::vector<QUBIT*> free_qubits_;

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

    size_t pending_count_{0};
public:
    ROTATION_SUBSYSTEM(double freq_khz, 
                        size_t capacity,
                        COMPUTE_SUBSYSTEM* parent,
                        double watermark);
    ~ROTATION_SUBSYSTEM();

    /*
     * Returns true if a rotation request can be allocated a
     * qubit.
     * */
    bool can_accept_rotation_request() const;

    /*
     * Adds a rotation request to be completed. Returns true if
     * the request was allocated a qubit, false otherwise.
     * */
    bool submit_rotation_request(inst_ptr);

    /*
     * Returns true if the rotation instruction is already pending
     * */
    bool is_rotation_pending(inst_ptr) const;

    /*
     * Returns true if the rotation is complete on some given qubit.
     * */
    bool find_and_delete_rotation_if_done(inst_ptr);

    /*
     * Returns number of uops retired for the given rotation
     * */
    size_t get_rotation_progress(inst_ptr) const;

    /*
     * Invalidates the rotation entry
     * */
    void invalidate_rotation(inst_ptr);
protected:
    long operate() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_ROTATION_SUBSYSTEM_h
