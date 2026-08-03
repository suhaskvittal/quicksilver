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
#include "sim/operable.h"
#include "sim/routing.h"
#include "sim/routing/multi_channel_bus.h"
#include "stats.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

extern bool GL_RAD_ENABLED;

class RAD;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class Driver : public sim::Operable
{
public:
    using inst_ptr = Instruction*;
    using dag_ptr = std::unique_ptr<DAG>;

    /*
     * Configuration type:
     * */
    struct config_type
    {
        /*
         * Driver params:
         * */
        double  reaction_time;
        int64_t decoder_count,
                code_distance;

        /*
         * RAD params:
         * */
        struct
        {
            /*
             * These variables are not the same as the driver
             * variables. Specify the faster decoder for 
             * `Driver`'s parameters, and specify the slower
             * decoder for `RAD`'s parameters.
             * */
            double  reaction_time;
            int64_t decoder_count;
            double  fast_decoder_error_probability;
            int64_t retired_dag_capacity;
        } rad;
    };

    /*
     * Routing data type. We implement the routing space as
     * multiple rows of qubits. Specifics of implementation,
     * such as number of program qubits per row, are in the
     * source file.
     * */
    struct routing_type : public sim::routing::MultiChannelBus<routing_type>
    {
        using sim::routing::MultiChannelBus<routing_type>::id_type;
        
        routing_type(size_t n);
    };

    const DecoderTraits decoder_traits;
    const size_t decoder_count,
                 code_distance;

    /*
     * Stats:
     * */
    uint64_t s_inst_done{0},
             s_inst_read{0},
             s_t_gates_in_program_done{0},
             s_t_gates_done{0};

    stats::Histogram<uint64_t> s_t_latency{"T_LATENCY", 0, 1000, 10};

    stats::Histogram<size_t> s_cx_routing_overhead{"CX_ROUTING_OVERHEAD", 0, 32, 8},
                             s_t_routing_overhead{"T_ROUTING_OVERHEAD", 0, 32, 8};
private:
    /*
     * Stream for workload:
     * */
    generic_strm_type trace_strm_;
    size_t program_qubits_;

    /*
     * Instruction DAG:
     * */
    dag_ptr dag_;

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

    /*
     * This will be initialized to the decoder's reaction time to
     * simulate that we are in the "middle" of a computation.
     * */
    cycle_type decoder_next_available_cycle_;

    /*
     * Routing logic:
     * */
    std::unique_ptr<routing_type> routing_;

    /*
     * RAD (may not be defined)
     * */
    std::unique_ptr<RAD> rad_;
public:
    Driver(std::string trace_file, config_type);
    ~Driver();

    long operate() override;

    void print_progress(std::ostream&) const;
    void print_deadlock_info(std::ostream&) const;

    bool eof() const { return generic_strm_eof(trace_strm_); }
    bool trace_exhausted() const { return eof() && dag_->inst_count() == 0; }

    double ipc() const { return fpdiv(s_inst_done, current_cycle()); }
    size_t program_qubits() const { return program_qubits_; }

    const std::unique_ptr<RAD>& rad() const { return rad_; }
private:
    void fetch_into_dag();
    bool execute_instruction(inst_ptr);
    void decode_history();
    void retire_instruction(inst_ptr);

    bool handle_routing(inst_ptr);

    void update_stats(inst_ptr);

    /*
     * Functions for operating over a given dag (calls functions above)
     * */
    long retire_instructions_from_dag(dag_ptr&);
    long execute_instructions_from_dag(const dag_ptr&, bool ignore_wrong_path_blockage);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#endif // RS_SIM_h
