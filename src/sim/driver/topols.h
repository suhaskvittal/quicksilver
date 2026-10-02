/*
 *  author: Suhas Vittal
 *  date:   30 September 2026
 * */

#ifndef SIM_DRIVER_TOPOLS_h
#define SIM_DRIVER_TOPOLS_h

#include "instruction.h"
#include "sim/client.h"
#include "sim/compute_subsystem.h"
#include "stats.h"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace sim
{
namespace driver
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * This implements the efficient Rz construction of TopoLS.
 * Basically, we entangle the program qubit with an ancilla qubit
 * in `qubits_` and perform the rotation gate on the ancilla. The
 * program qubit cannot perform another rotation gate until the 
 * ancilla qubit completes, unless there's a degenerate structure
 * like RZ-CZ-RZ which should never occur if the compiler/programmer
 * is smart.
 * */
class TopoLS
{
public:
    using inst_ptr = Instruction*;

    struct request_type
    {
        Client* client;
        inst_ptr inst{nullptr};
        Qubit* pinned_qubit;

        std::unordered_set<Qubit*> dependent_qubits;
        std::vector<inst_ptr> blocked_inst;

        bool done{false};
        uint64_t gates_hidden{0};

        /*
         * `start_cycle` is when this request first began executing.
         * `block_start_cycle` is the first cycle where this request started blocking another instruction in
         *      the program.
         * */
        cycle_type start_cycle =std::numeric_limits<cycle_type>::max(),
                   block_start_cycle =std::numeric_limits<cycle_type>::max();
    };

    uint64_t s_requests_allocated{0};
    uint64_t s_requests_full_latency_hidden{0};

    stats::Histogram<uint64_t> s_latency{"LATENCY", 0, 20000, 10};
    stats::Histogram<uint64_t> s_hidden_gates{"HIDDEN_GATES", 0, 1000, 10};
    stats::Histogram<uint64_t> s_hidden_latency{"HIDDEN_LATENCY", 0, 20000, 10};
    stats::Histogram<double>   s_fraction_hidden_latency{"HIDDEN_LATENCY_FRACTION", 0, 1.0, 10};
private:
    /*
     * `free_qubits_` corresponds to all available ancilla qubits ready
     * to do an Rz gate.
     * */
    std::vector<Qubit*> free_qubits_;
    std::vector<request_type> active_requests_;

    bool request_recently_retired_{false};

    ComputeSubsystem* compute_subsystem_;
public:
    TopoLS(ComputeSubsystem*);

    long operate();

    void allocate(Client*, inst_ptr);
    void update_on_instruction_issue(Client*, inst_ptr);

    /*
     * This function first returns true if the given instruction cannot be
     * executed yet. If this function returns true, then we also set 
     * `request_type::block_start_cycle` for the blocking instruction.
     * */
    bool is_instruction_blocked(Client*, inst_ptr);

    cycle_type current_cycle() const { return compute_subsystem_->current_cycle(); }
    size_t capacity() const { return qubits_.size(); }
    bool has_free_ancilla() const { return free_qubits_.size() > 0; }
private:
    void retire_request(const request_type&);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace driver
} // namespace sim

#endif 
