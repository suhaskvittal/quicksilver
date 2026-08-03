/*
 *  author: Suhas Vittal
 *  date:   14 July 2026
 * */

#ifndef RS_RAD_h
#define RS_RAD_h

#include "qs_reaction_sim/history.h"

#include "dag.h"
#include "decoder_traits.h"
#include "globals.h"
#include "stats.h"

#include <memory>
#include <unordered_set>
#include <vector>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class RAD
{
public:
    constexpr static size_t RECURSION_DEPTH_MAX{4};

    using inst_ptr = DAG::inst_ptr;
    using dag_ptr = std::unique_ptr<DAG>;
    using blocked_set_type = std::unordered_set<qubit_type>;

    /*
     * Wrong-path lifecycle:
     *  --> INVALID   : no wrong path in flight.
     *  --> RESOLVING : an error was detected; the main program is stalled while
     *                  the tainted region is determined (`retired_dag_` drains and
     *                  the syndrome history decodes down to idles).
     *  --> EXECUTING : the `wrong_path_dag_` (uncompute + recompute) is running.
     *  --> TRANSIENT : in the middle of RESOLVING and EXECUTING (finishing up decoding for
     *                  executed non-Cliffords, but also blocking execution due to resolution)
     * */
    enum class WrongPathState { INVALID, RESOLVING, EXECUTING, TRANSIENT };

    const double fast_decoder_error_probability;

    /*
     * `RAD` will only track the slow decoder. Fast decoder
     * should be implemented in `Driver`
     * */
    const DecoderTraits slow_decoder_traits;
    const size_t program_qubits,
                 decoder_count,
                 code_distance,
                 retired_dag_capacity;

    /*
     * Statistics
     * */
    stats::Histogram<size_t> s_retired_dag_occu{"RETIRED_DAG_OCCU", 0, 1024, 16};

    uint64_t s_wrong_paths{};
    stats::Histogram<uint64_t> s_wrong_path_latency{"WRONG_PATH_LATENCY", 0, 10000, 10};
    stats::Histogram<size_t>   s_wrong_path_inst_count{"WRONG_PATH_INST_COUNT", 0, 1024, 8};
    stats::Histogram<size_t>   s_qubits_blocked_by_wrong_path{"QUBITS_BLOCKED_DURING_WRONG_PATH", 0, 8192, 16};
    stats::Histogram<size_t>   s_wrong_path_recursion_depth{"WRONG_PATH_RECURSION_DEPTH", 0, 16, 8};

    uint64_t s_cycles_locked_to_l2_decoder{0},
             s_cycles_main_program_stalled{0};
private:
    /*
     * This contains a DAG full of retired instructions. Once an
     * instruction is committed, we remove from the DAG (or mark
     * it for removal if it is not in the front layer).
     * */
    dag_ptr retired_dag_;

    /*
     * When we encounter an error, we need to starting initializing
     * the `wrong_path_dag()`. We stall the main program while
     * wrong path resolution occurs. Once resolution finishes (`retired_dag_`
     * is empty), we execute `wrong_path_dag()`.
     * */
    std::array<dag_ptr, RECURSION_DEPTH_MAX> wrong_path_dag_array_{};
    size_t wrong_path_idx_{0};

    blocked_set_type wrong_path_blocked_qubits_;
    WrongPathState wrong_path_state_{WrongPathState::INVALID};

    /*
     * During resolution, track instructions to put into `wrong_path_dag_`
     * using `wrong_path_uncomp_` and `wrong_path_recomp_`.
     *
     * If we find a new error while executing `wrong_path_dag_`, then we
     * move the remaining instructions in `wrong_path_dag_` into
     * `wrong_path_incomplete_` and reconstruct the `wrong_path_dag_`
     * */
    std::vector<inst_ptr> wrong_path_uncomp_,
                          wrong_path_recomp_;
    cycle_type wrong_path_start_cycle_,
               wrong_path_inst_count_;
    size_t wrong_path_max_recursion_depth_{0};

    std::unique_ptr<History> syndrome_history_;
    std::unordered_map<qubit_type, cycle_type> anc_available_cycle_;

    cycle_type slow_decoder_next_available_cycle_;
public:
    RAD(size_t program_qubits,
        size_t decoder_count,
        size_t code_distance,
        size_t retired_dag_capacity,
        DecoderTraits slow_decoder_traits,
        double fast_decoder_error_probability);

    long operate(cycle_type current_cycle);

    /*
     * We need to expose two functions for updating the syndrome history
     * */
    void register_instruction_in_history(inst_ptr, cycle_type current_cycle, cycle_type avail_cycle);
    void add_idle_cycle(cycle_type current_cycle,
                        const std::vector<cycle_type>& program_qubit_available_cycle);

    /*
     * retire vs commit:
     *  retire simply means to move out of the execution window.
     *  commit means that we know that the instruction occurred correctly.
     * */
    void retire_instruction(inst_ptr);
    void commit_instruction(inst_ptr);

    void retire_wrong_path_inst(inst_ptr);

    bool stop_using_fast_decoder() const;
    
    bool is_qubit_blocked_by_wrong_path(qubit_type q) const { return wrong_path_blocked_qubits_.count(q) > 0;}
    bool is_resolving_wrong_path() const { return wrong_path_state_ == WrongPathState::RESOLVING; }
    bool is_executing_wrong_path() const { return wrong_path_state_ == WrongPathState::EXECUTING; }
    bool is_waiting_for_fast_decoder_before_resolution() const { return wrong_path_state_ == WrongPathState::TRANSIENT; }

    const dag_ptr& retired_dag() const { return retired_dag_; }
    const std::unique_ptr<History>& history() const { return syndrome_history_; }

    size_t wrong_path_recursion_depth() const { return wrong_path_idx_; }
    bool stall_main_program() const { return wrong_path_idx_ >= RECURSION_DEPTH_MAX-2; }

    /*
     * Need to expose both const and non-const for `wrong_path_dag_` since `Driver` is responsible
     * for removing instructions from it.
     * */
    auto& wrong_path_dag(this auto& r) { return r.wrong_path_dag_array_[r.wrong_path_idx_]; }
private:
    long handle_commit(cycle_type);
    void decode_history(cycle_type);

    /*
     * Handles errors originating from decoding errors on the given
     * instruction. Returns true if we cannot move into the `RESOLVING`
     * state.
     * */
    bool handle_decoding_error(inst_ptr, cycle_type current_cycle);

    void try_and_add_to_wrong_path(inst_ptr);
    void initialize_wrong_path();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif // RS_RAD_h
