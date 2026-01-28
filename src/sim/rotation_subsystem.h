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

    uint64_t s_rotations_completed{0};
private:
    /*
     * Stores assignment of rotation gates to logical qubits.
     * If a rotation instruction points to a null qubit, then
     * the rotation is done.
     * */
    std::unordered_map<inst_ptr, QUBIT*> rotation_assignment_map_;
    std::vector<QUBIT*>                  free_qubits_;

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
public:
    ROTATION_SUBSYSTEM(double freq_khz, 
                        size_t capacity,
                        std::vector<T_FACTORY_BASE*> top_level_t_factories,
                        MEMORY_SUBSYSTEM* /* useless, but still provide */,
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
protected:
    long operate() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif // SIM_ROTATION_SUBSYSTEM_h
