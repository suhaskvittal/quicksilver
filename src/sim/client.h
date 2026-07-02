/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef SIM_CLIENT_h
#define SIM_CLIENT_h

#include "dag.h"
#include "generic_io.h"
#include "globals.h"
#include "sim/qubit.h"
#include "stats.h"

#include <limits>
#include <memory>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Client
{
public:
    using inst_ptr = DAG::inst_ptr;
    using uint_hist_type = stats::Histogram<uint64_t>;

    /*
     * Statistics (only variables prefixed by `s_`)
     * */
    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_unrolled_inst_done{0};
    uint64_t s_t_gates_done{0};
    uint64_t s_rotations_done{0};
    uint64_t s_memory_accesses_done{0};
    uint64_t s_cycle_complete{std::numeric_limits<uint64_t>::max()};

    uint_hist_type s_rotation_latency{"ROTATION_LATENCY", 0, 10000, 10};
    uint_hist_type s_rotation_uops{"ROTATION_UOPS", 0, 256, 8};
    uint_hist_type s_memory_access_latency{"MEMORY_LATENCY", 0, 1000, 10};

    const std::string    trace_file;
    const client_id_type id;
private:
    /*
     * We have this wonky order because we need to
     * initialize `tristrm_` and open it before
     * setting `num_qubits`
     * */
    generic_strm_type    tristrm_;
public:
    const size_t         num_qubits;
private:
    std::unique_ptr<DAG> dag_;
    bool                 has_hit_eof_once_{false};

    std::vector<Qubit*> qubits_;
public:
    Client(std::string trace_file, client_id_type);
    ~Client();

    /*
     * Warms up the dag by filling it with instructions
     * until it reaches the given size.
     * */
    void warmup_dag(size_t);

    /*
     * This gets all instructions in `dag_`'s front layer
     * that meet the predicate.
     *
     * For example, this predicate could be all qubits
     * that are ready and in the compute subsystem.
     *
     * Note that since this predicate is being given to
     * `dag_`, the input to the predicate is an instruction
     * pointer.
     * */
    template <class Pred>
    std::vector<inst_ptr> get_ready_instructions(const Pred&);

    void retire_instruction(inst_ptr);

    bool eof() const;

    const std::unique_ptr<DAG>& dag() const;
    const std::vector<Qubit*>&  qubits() const;
private:
    size_t   open_file_and_read_qubit_count();
    inst_ptr read_instruction_from_trace();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation of `Client::get_ready_instructions(const Pred&)`
 * */

template <class Pred> std::vector<Client::inst_ptr>
Client::get_ready_instructions(const Pred& pred)
{
    constexpr size_t DAG_WATERMARK = 16384;
    warmup_dag(DAG_WATERMARK);
    return dag_->get_front_layer_if(pred);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_CLIENT_h
