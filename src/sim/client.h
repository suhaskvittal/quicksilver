/*
 *  author: Suhas Vittal
 *  date:   4 January 2026
 * */

#ifndef SIM_CLIENT_h
#define SIM_CLIENT_h

#include "dag.h"
#include "generic_io.h"
#include "globals.h"

#include <limits>
#include <memory>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class CLIENT
{
public:
    using inst_ptr = DAG::inst_ptr;

    /*
     * Statistics (only variables prefixed by `s_`)
     * */
    uint64_t s_inst_read{0};
    uint64_t s_inst_done{0};
    uint64_t s_unrolled_inst_done{0};
    uint64_t s_cycle_complete{std::numeric_limits<uint64_t>::max()};

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

    std::vector<QUBIT*> qubits_;
public:
    CLIENT(std::string trace_file, client_id_type);
    ~CLIENT();

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
    template <class PRED>
    std::vector<inst_ptr> get_ready_instructions(const PRED&);

    void retire_instruction(inst_ptr);

    bool eof() const;

    const std::unique_ptr<DAG>& dag() const;
    const double                ipc() const;
    const std::vector<QUBIT*>&  qubits() const;
private:
    size_t   open_file_and_read_qubit_count();
    inst_ptr read_instruction_from_trace();
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Implementation of `CLIENT::get_ready_instructions(const PRED&)`
 * */

template <class PRED> std::vector<CLIENT::inst_ptr>
CLIENT::get_ready_instructions(const PRED& pred)
{
    constexpr size_t DAG_WATERMARK = 16384;
    // fill up the DAG if it is below some count:
    while (dag_->inst_count() < DAG_WATERMARK)
    {
        inst_ptr inst = read_instruction_from_trace();
        // immediately elide software instructions here
        if (is_software_instruction(inst->type))
            delete inst;
        else
            dag_->add_instruction(inst);
    }
    return dag_->get_front_layer_if(pred);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_CLIENT_h
