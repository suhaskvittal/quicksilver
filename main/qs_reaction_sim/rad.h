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
    using inst_ptr = DAG::inst_ptr;
    using dag_ptr = std::unique_ptr<DAG>;
    using blocked_set_type = std::unordered_set<qubit_type>;

    const double fast_decoder_error_probability;

    /*
     * `RAD` will only track the slow decoder. Fast decoder
     * should be implemented in `Driver`
     * */
    const DecoderTraits slow_decoder_traits;
    const size_t program_qubits;
    const size_t decoder_count;
private:
    /*
     * This contains a DAG full of retired instructions. Once an
     * instruction is committed, we remove from the DAG (or mark
     * it for removal if it is not in the front layer).
     * */
    dag_ptr retired_dag_;

    /*
     * When we encounter an error, we need to starting initializing
     * the `wrong_path_dag_`. We stall the main program while
     * wrong path resolution occurs. Once resolution finishes (`retired_dag_`
     * is empty), we execute `wrong_path_dag_`.
     * */
    dag_ptr wrong_path_dag_;
    blocked_set_type wrong_path_blocked_qubits_;
    bool wrong_path_resolution_in_progress_{false},
         wrong_path_execution_in_progress_{false};

    /*
     * During resolution, track instructions to put into `wrong_path_dag_`
     * using `wrong_path_uncomp_` and `wrong_path_recomp_`.
     *
     * If we find a new error while executing `wrong_path_dag_`, then we
     * move the remaining instructions in `wrong_path_dag_` into
     * `wrong_path_incomplete_` and reconstruct the `wrong_path_dag_`
     * */
    std::vector<inst_ptr> wrong_path_uncomp_,
                          wrong_path_recomp_,
                          wrong_path_incomplete_;

    std::unique_ptr<History> syndrome_history_;
    std::unordered_map<qubit_type, cycle_type> anc_available_cycle_;

    cycle_type slow_decoder_next_available_cycle_;
public:
    RAD(size_t program_qubits,
        size_t decoder_count,
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
    
    bool is_qubit_blocked_by_wrong_path(qubit_type q) const { return wrong_path_blocked_qubits_.count(q) > 0;}
    bool is_resolving_wrong_path() const { return wrong_path_resolution_in_progress_; }

    const std::unique_ptr<History>& history() const { return syndrome_history_; }

    /*
     * Need to expose both const and non-const for `wrong_path_dag_` since `Driver` is responsible
     * for removing instructions from it.
     * */
    auto& wrong_path_dag(this auto& r) { return r.wrong_path_dag_; }
private:
    long handle_commit();
    long handle_wrong_path_retires();
    void decode_history(cycle_type);

    /*
     * Handles errors originating from decoding errors on the given
     * instructions (the front-layer T gates the slow decoder just flagged).
     * */
    void handle_decoding_error(std::vector<inst_ptr> tainted_inst);
    void initialize_wrong_path();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif // RS_RAD_h
