/*
 *  author: Suhas Vittal
 *  date:   16 June 2026
 * */

#include "argparse.h"
#include "generic_io.h"
#include "instruction.h"
#include "compiler/memory_scheduler.h"
#include "compiler/memory_scheduler/impl.h"

#include "ortools/sat/cp_model.h"
#include "ortools/sat/sat_parameters.pb.h"
#include "ortools/util/sorted_interval_list.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <unistd.h>   // close, mkstemp

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = INSTRUCTION*;
using stats_type = compile::memory_scheduler::stats_type;
using config_type = compile::memory_scheduler::config_type;

/*
 * Writes the first `inst_limit` instructions of `istrm` to `ostrm`, remapping
 * qubits to dense ids in order of first appearance and emitting a compacted
 * qubit-count header (= number of distinct qubits in the window).
 *
 * Dies if the window touches fewer than `capacity` distinct qubits, since the
 * initial active set {0..capacity-1} would otherwise be undefined.
 * */
void create_truncated_file(generic_strm_type& ostrm, generic_strm_type& istrm,
                           int64_t inst_limit, int64_t capacity);

/*
 * Runs a single memory scheduler over the truncated trace at `trace_path`
 * and returns its stats. A fresh input stream is opened per call (the
 * scheduler consumes the stream to EOF); the rewritten program is discarded.
 * */
template <class SCHEDULER_IMPL>
stats_type run_scheduler(const std::string& trace_path, const SCHEDULER_IMPL&, config_type);

/*
 * Builds and solves the memory-access-optimal schedule as an ILP (via OR-Tools
 * CP-SAT) over the truncated trace, and returns the optimal number of memory
 * accesses. Returns -1 if the solver could not prove anything usable.
 *
 * Model (layers t are 0-indexed, t in [0,n); init[q] plays the role of the
 * free initial residency C(q,0) in the derivation):
 *
 *   minimize  sum_{q,t} m[q][t]
 *   s.t. (1)  sum_t I[j][t] = 1                       execute each instruction once
 *        (2)  I[a][t] <= sum_{r<t} I[b][r]   (b<a)    precedence / dependencies
 *        (3)  I[j][t] <= C[q][t]   (q in A_j)         operands resident to execute
 *        (4)  sum_q C[q][t] <= K                      compute-subsystem capacity
 *        (6)  m[q][t] >= C[q][t] - C[q][t-1]          count out->in crossings (loads)
 * */
int64_t opt(const std::string& trace_path, int64_t active_set_capacity);

std::string ilp_varname(std::string array_name, int64_t i, int64_t t);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string input_trace_file;
    int64_t     inst_limit;
    int64_t     capacity;

    ARGPARSE()
        .required("input-file", "Input trace file without memory instructions", input_trace_file)
        .optional("-i", "--inst-limit", "Number of instructions in the truncated trace", inst_limit, 100)
        .optional("-c", "--active-set-capacity", "Number of program qubits in the active set", capacity, 12)
        .parse(argc, argv);

    // 1. write the first `inst_limit` instructions to a temp trace file.
    char trace_path[] = "/tmp/qs_optimality_XXXXXX";
    int fd = mkstemp(trace_path);
    if (fd < 0)
        std::cerr << "qs_memory_optimality: could not create temp file" << _die{};
    close(fd);   // reopened by name through generic_strm_open below

    {
        generic_strm_type istrm, ostrm;
        generic_strm_open(istrm, input_trace_file, "rb");
        generic_strm_open(ostrm, trace_path, "wb");
        create_truncated_file(ostrm, istrm, inst_limit, capacity);
        generic_strm_close(istrm);
        generic_strm_close(ostrm);
    }

    // 2. run EIF and HINT over the truncated trace.
    //    The window is small (ILP-sized), so load it entirely into the DAG and
    //    let each scheduler run to completion. Scheduler-specific knobs keep
    //    their `config_type` defaults.
    config_type conf;
    conf.active_set_capacity      = capacity;
    conf.inst_compile_limit       = std::numeric_limits<int64_t>::max();  // run to EOF
    conf.dag_inst_capacity        = inst_limit;   // hold the whole window at once
    conf.print_progress_frequency = 0;            // silence progress output
    conf.hint_lookahead_depth = 256;
    conf.eif_lookahead_depth = 256;
    conf.hint_use_complex_selection = true;
    conf.hint_use_nonarbitrary_victim_selection = true;
    conf.count_whole_instructions = true;

    stats_type eif_stats  = run_scheduler(trace_path, compile::memory_scheduler::eif,  conf);
    stats_type hint_stats = run_scheduler(trace_path, compile::memory_scheduler::hint, conf);

    // 3. run the ILP for the optimal number of memory accesses.
    int64_t opt_accesses = opt(trace_path, capacity);

    // report:
    print_stat_line(std::cout, "INST_LIMIT",           inst_limit);
    print_stat_line(std::cout, "ACTIVE_SET_CAPACITY",  capacity);
    print_stat_line(std::cout, "EIF_MEMORY_ACCESSES",  eif_stats.memory_accesses);
    print_stat_line(std::cout, "HINT_MEMORY_ACCESSES", hint_stats.memory_accesses);
    print_stat_line(std::cout, "OPT_MEMORY_ACCESSES",  opt_accesses);

    std::remove(trace_path);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
create_truncated_file(generic_strm_type& ostrm, generic_strm_type& istrm, int64_t inst_limit, int64_t capacity)
{
    // skip the original (full-program) qubit count; the truncated trace gets a
    // new, compacted header below.
    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, sizeof(num_qubits));

    // Read the window, remapping qubits to dense ids in order of first
    // appearance. We buffer because the new header (= #distinct qubits) isn't
    // known until the whole window has been scanned.
    std::unordered_map<qubit_type, qubit_type> remap;
    std::vector<INSTRUCTION*>                  buffer;
    buffer.reserve(inst_limit);

    int64_t i{0};
    while (i < inst_limit)
    {
        INSTRUCTION* inst = read_instruction_from_stream(istrm);
        if (generic_strm_eof(istrm))   // last read ran past the end -> discard
        {
            delete inst;
            break;
        }

        if (is_software_instruction(inst->type))
            continue;

        std::vector<qubit_type> qubits(inst->q_begin(), inst->q_end());
        for (auto& q : qubits)
        {
            auto [it, inserted] = remap.try_emplace(q, static_cast<qubit_type>(remap.size()));
            q = it->second;
        }

        INSTRUCTION* out = new INSTRUCTION{inst->type, qubits.begin(), qubits.end(),
                                           inst->angle, inst->urotseq.begin(), inst->urotseq.end()};
        out->corr_urotseq_array = inst->corr_urotseq_array;
        buffer.push_back(out);
        delete inst;
        i++;
    }

    // The initial active set is {0..capacity-1}; if the window doesn't even
    // touch `capacity` distinct qubits, that set is undefined -- bail out.
    const int64_t distinct_qubits = static_cast<int64_t>(remap.size());
    if (distinct_qubits < capacity)
    {
        std::cerr << "create_truncated_file: window touches only " << distinct_qubits
                  << " distinct qubits, fewer than the active-set capacity (" << capacity
                  << "); cannot define the initial active set" << _die{};
    }
    std::cout << "distinct qubits = " << distinct_qubits << "\n";

    // write the compacted header, then the remapped window.
    uint32_t out_num_qubits = static_cast<uint32_t>(distinct_qubits);
    generic_strm_write(ostrm, &out_num_qubits, sizeof(out_num_qubits));
    for (INSTRUCTION* out : buffer)
    {
        write_instruction_to_stream(ostrm, out);
        delete out;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SCHEDULER_IMPL> stats_type
run_scheduler(const std::string& trace_path, const SCHEDULER_IMPL& scheduler, config_type conf)
{
    generic_strm_type istrm, ostrm;
    generic_strm_open(istrm, trace_path, "rb");
    generic_strm_open(ostrm, "/dev/null", "wb");   // throwaway output

    auto stats = compile::memory_scheduler::run(ostrm, istrm, scheduler, conf);

    generic_strm_close(istrm);
    generic_strm_close(ostrm);
    return stats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int64_t
opt(const std::string& trace_path, int64_t active_set_capacity)
{
    /*
     * Useful typedefs + namespace shorthand:
     * */
    namespace sat = operations_research::sat;
    using ilp_type = sat::CpModelBuilder;
    using bvar_type = sat::BoolVar;

    const int64_t k = active_set_capacity;

    generic_strm_type istrm;
    generic_strm_open(istrm, trace_path, "rb");

    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, sizeof(num_qubits));

    /*
     * 1. Get relevant information from `istrm` about program.
     *  We care about the instructions, dependences, etc.
     * */

    std::vector<inst_ptr> inst_list;
    std::unordered_map<inst_ptr, std::vector<inst_ptr>> dependents;
    std::vector<inst_ptr> last_inst_for_qubit(num_qubits, nullptr);
    inst_list.reserve(128);
    dependents.reserve(128);
    while (true)
    {
        auto* inst = read_instruction_from_stream(istrm); 
        if (generic_strm_eof(istrm))
        {
            delete inst;
            break;
        }
        for (auto q_it = inst->q_begin(); q_it != inst->q_end(); q_it++)
        {
            inst_ptr prev = last_inst_for_qubit[*q_it];
            if (prev != nullptr)
                dependents[prev].push_back(inst);
            last_inst_for_qubit[*q_it] = inst;
        }
        inst_list.push_back(inst);
    }
    std::unordered_map<inst_ptr, int64_t> inst_to_idx;
    inst_to_idx.reserve(inst_list.size());
    for (int64_t i = 0; i < inst_list.size(); i++)
        inst_to_idx[inst_list[i]] = i;
    
    /*
     * 2. Build ILP
     * */
    ilp_type prob;
        
    const int64_t n = num_qubits;
    const int64_t m = inst_list.size();
    std::vector<bvar_type> Q(n*m);  // Q(j,t) = is qubit j in compute subsystem at time t
    std::vector<bvar_type> I(m*m);  // I(j,t) = is instruction j executed at time t
    std::vector<bvar_type> M(n*m);  // M(j,t) = did qubit j get stored between time t-1 and time t

    for (int64_t i = 0; i < n; i++)
    {
        for (int64_t t = 0; t < m; t++)
        {
            Q[i*m+t] = prob.NewBoolVar().WithName(ilp_varname("Q", i, t));
            M[i*m+t] = prob.NewBoolVar().WithName(ilp_varname("M", i, t));
        }
    }

    for (int64_t i = 0; i < m; i++)
        for (int64_t j = 0; j < m; j++)
            I[i*m+j] = prob.NewBoolVar().WithName(ilp_varname("I", i, j));

    // C1: instruction dependency constraint
    for (auto* a : inst_list)
    {
        auto a_it = dependents.find(a);
        if (a_it == dependents.end())
            continue;

        // create LHS sum:
        auto i = inst_to_idx.at(a);
        auto a_begin = I.begin() + i*m;
        for (int64_t t = 1; t < m; t++)
        {
            auto a_end = a_begin + t;
            std::vector<bvar_type> lhs(a_begin, a_end);
            for (auto* b : a_it->second)
            {
                auto j = inst_to_idx.at(b);
                prob.AddGreaterOrEqual(sat::LinearExpr::Sum(lhs), I[j*m+t]);
            }
        }
        // also prevent scheduling `b` at layer 0.
        for (auto* b : a_it->second)
        {
            auto j = inst_to_idx.at(b);
            prob.AddEquality(I[j*m], 0);
        }
    }

    // C2: uniqueness constraint
    for (int64_t i = 0; i < m; i++)
    {
        auto begin = I.begin() + i*m;
        auto end = begin + m;
        std::vector<bvar_type> lhs(begin, end);
        prob.AddEquality(sat::LinearExpr::Sum(lhs), 1);
    }

    // C3: residency constraint
    for (int64_t t = 0; t < m; t++)
    {
        for (int64_t i = 0; i < m; i++)
        {
            auto* inst = inst_list[i];
            for (auto q_it = inst->q_begin(); q_it != inst->q_end(); q_it++)
                prob.AddLessOrEqual(I[i*m+t], Q[(*q_it)*m+t]);
        }
    }

    // C4: capacity constraint
    for (int64_t t = 0; t < m; t++)
    {
        std::vector<bvar_type> lhs;
        lhs.reserve(n);
        for (int64_t i = 0; i < n; i++)
            lhs.push_back( Q[i*m+t] );
        prob.AddLessOrEqual(sat::LinearExpr::Sum(lhs), k);
    }

    // C5: identifying memory accesses:
    for (int64_t t = 1; t < m; t++)
        for (int64_t i = 0; i < n; i++)
            prob.AddGreaterOrEqual(M[i*m+t], Q[i*m+t]-Q[i*m+(t-1)]);

    // Initialization:
    for (int64_t i = 0; i < k; i++)
        prob.AddEquality(Q[i*m], 1);

    // Objective Function:
    prob.Minimize(sat::LinearExpr::Sum(M));

    /*
     * 3. Solve ILP
     * */
    sat::SatParameters params;
    params.set_max_time_in_seconds(3600.0);
    params.set_num_search_workers(8);
    params.set_log_search_progress(false);

    const sat::CpSolverResponse resp = sat::SolveWithParameters(prob.Build(), params);

    // done with the trace: free the instructions and close the stream.
    for (auto* inst : inst_list)
        delete inst;
    generic_strm_close(istrm);

    // OPTIMAL -> proven minimum; FEASIBLE -> best found within the time limit
    // (an upper bound, not proven optimal); anything else -> no usable answer.
    if (resp.status() == sat::CpSolverStatus::FEASIBLE)
        std::cerr << "opt: time limit hit -- returning best found (not proven optimal)\n";
    else if (resp.status() != sat::CpSolverStatus::OPTIMAL)
        return -1;

    return static_cast<int64_t>(std::llround(resp.objective_value()));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
ilp_varname(std::string s, int64_t i, int64_t j)
{
    return s + "[" + std::to_string(i) + "," + std::to_string(j) + "]";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
