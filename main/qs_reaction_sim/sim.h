/*
 * author: Suhas Vittal
 * date:    10 July 2026
 * */

#ifndef RS_SIM_h
#define RS_SIM_h

#include "qs_reaction_sim/history.h"

#include "dag.h"
#include "decoder_traits.h"
#include "generic_io.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Driver
{
public:
    using inst_ptr = Instruction*;

    const DecoderTraits dec_traits;
    const DecodingMethod dec_method;
    const size_t decoder_count,
                 code_distance;

    /*
     * Stats:
     * */
    uint64_t s_inst_done{0},
             s_inst_read{0};
private:
    /*
     * Stream for workload:
     * */
    generic_strm_type trace_strm_;
    size_t program_qubits_;

    /*
     * Instruction DAG:
     * */
    std::unique_ptr<DAG> dag_;

    /*
     * Syndrome history:
     * */
    std::unique_ptr<History> syndrome_history_;

    /*
     * Once an operation happens, we need to track how long the
     * qubit will be busy for, as we do not want to insert idle
     * cycles during this time.
     *
     * We also need to track some ancillas separately, specifically
     * those resulting from the execution of a non-Clifford.
     * */
    std::vector<cycle_type> program_qubit_available_cycle_;
    std::unordered_map<qubit_type, cycle_type> anc_available_cycle_;

    cycle_type current_cycle_{0};
    cycle_type decoder_avail_next_cycle_{0};
public:
    Driver(std::string trace_file, 
            DecoderTraits,
            DecodingMethod,
            size_t decoder_count,
            size_t code_distance);
    ~Driver();

    long operate();

    double ipc() const { return fpdiv(s_inst_done, current_cycle_); }
    cycle_type current_cycle() const { return current_cycle_; }

    size_t program_qubits() const { return program_qubits_; }
private:
    void fetch_into_dag();
    void retire_instruction(inst_ptr);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif // RS_SIM_h
