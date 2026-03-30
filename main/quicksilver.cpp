/*
 *  author: Suhas Vittal
 *  date:   16 January 2026
 * */

#include "sim.h"
#include "sim/configuration/allocator/impl.h"
#include "sim/configuration/predefined_ed_protocols.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/compute_subsystem.h"
#include "sim/driver.h"
#include "sim/memory/bivariate_bicycle.h"
#include "sim/memory_level.h"
#include "sim/production/epr.h"
#include "sim/production/magic_state.h"

#include "compiler/pass/memory_scheduler.h"
#include "compiler/pass/memory_scheduler/impl.h"

#include "argparse/argparse.h"

#include <sys/stat.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace 
{

using FACTORY_SPECIFICATION = sim::configuration::FACTORY_SPECIFICATION;
using ED_SPECIFICATION = sim::configuration::ED_SPECIFICATION;

struct FIDELITY_RESULT
{
    /*
     * Fidelity breakdown:
     * */
    double overall;
    double compute_subsystem;
    double memory_subsystem;
    double magic_state;
};

std::vector<std::string> split_trace_string(std::string);

/*
 * Compiles the given trace by performing memory access scheduler. The `trace`
 * reference is then overwritten with the new trace.
 * */
void jit_compile(std::string& trace, int64_t inst_sim, int64_t active_set_capacity);

/*
 * Retrieves the number of qubits for the given trace:
 * */
size_t get_number_of_qubits(std::string_view);

/*
 * Returns the code distance for the given error-rate regime
 * */
size_t get_compute_code_distance(std::string_view);
size_t get_memory_code_distance(std::string_view);

/*
 * These two functions get the default production specifications for magic state factories
 * and entanglement distillation (parametrized by values that can be extended by the user).
 * */
std::vector<FACTORY_SPECIFICATION> get_default_factory_specifications(std::string_view regime,
                                                                      int64_t compute_cycle_time_ns,
                                                                      int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION>      get_default_ed_specifications(std::string_view regime,
                                                                    int64_t compute_cycle_time_ns,
                                                                    int64_t ll_buffer_capacity);

/*
 * Computes the number of physical qubits required for ED on the faster substrate.
 * Unlike the slower substrate, the faster substrate can highly serialize ED, and does
 * not need many ED units either, so the overhead of ED is just the number of physical
 * qubits required for the last level of ED (first argument) and the amount of idling
 * time (can be computed using `substrate_mismatch_factor` and `ED_SPECIFICATION`)
 * */
size_t faster_substrate_ed_overhead(ED_SPECIFICATION&, int64_t substrate_mismatch_factor);

/*
 * Computes the probability of success post-simulation
 * */
FIDELITY_RESULT compute_application_fidelity(uint64_t scale_to_instructions, sim::CLIENT*, sim::COMPUTE_SUBSYSTEM*);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    sim::GL_RNG.seed(0);

    std::string trace_string;
    int64_t     inst_sim;

    int64_t print_progress;
    int64_t ratemode;
    int64_t skip_threshold;
    bool    jit;
    std::string regime;
    int64_t total_inst;

    int64_t concurrent_clients;
    int64_t compute_local_memory_capacity;
    int64_t compute_cycle_time_ns;

    int64_t memory_cycle_time_ns;

    int64_t factory_ll_buffer_capacity;
    int64_t factory_physical_qubit_budget;

    ARGPARSE()
        .required("trace string", "Path to trace file (if single file or ratemode > 1), or paths separated by `;`", trace_string)
        .required("simulation instructions", "Number of instructions to simulate (for each workload)", inst_sim)

        .optional("-pp", "--print-progress", "Progress print frequency (in compute cycles)", print_progress, 0)
        .optional("", "--ratemode", "If a single trace file is provided, then number of clients using that file", ratemode, 1)
        .optional("", "--skip-threshold", "Number of cycles without progress before skipping cycles", skip_threshold, 100)
        .optional("-jit", "", "Just-in-time compilation for an input source file", jit, false)
        .optional("", "--regime", 
                    "Choose one of: M, G, T (megaquop, gigaqoup, terquop). This affects code distance + factory allocation",
                    regime, "T")
        .optional("", "--total-program-instructions", 
                    "Total instructions in the program. Used for resource estimates. Does not affect performance.",
                    total_inst, 1'000'000'000)

        .optional("-c", "--concurrent-clients", "Number of active concurrent clients", concurrent_clients, 1)
        .optional("-a", "--compute-local-memory-capacity", "Number of active qubits in the compute subsystem's local memory", 
                      compute_local_memory_capacity, 12)
        .optional("", "--compute-cycle-time-ns", 
                      "Syndrome extraction round latency for surface code (in nanoseconds)", 
                      compute_cycle_time_ns, 1200)

        .optional("", "--reaction-time", "Control reaction time (in compute cycles)", sim::GL_REACTION_TIME, 10)
        .optional("", "--rltp-degree", "Reaction-limited T teleportation degree", sim::GL_RLTP_DEGREE, 0)

        .optional("-rdr", "", "Enable rotation directed runahead", GL_USE_RDR_ISA, 0)
        .optional("", "--rdr-capacity", "Number of ancilla", sim::GL_RDR_CAPACITY, 2)
        .optional("", "--rdr-start-layer", "DAG layer to start runahead", sim::GL_RDR_START_LAYER, 2)
        .optional("", "--rdr-lookahead-depth", "Number of DAG layers to search", sim::GL_RDR_LOOKAHEAD_DEPTH, 8)
        .optional("", "--rdr-inst-delta-limit", "Instruction delta limit for runahead", sim::GL_RDR_INST_DELTA_LIMIT, 500)
        .optional("", "--rdr-degree", "Degree of runahead (number of instructions)", sim::GL_RDR_DEGREE, 2)
        .optional("", "--rdr-enable-perfect-completion-buffer",
                      "Enable perfect completion buffer for RDR",
                      sim::GL_RDR_ENABLE_PERFECT_COMPLETION_BUFFER, false)

        .optional("", "--memory-cycle-time-ns", 
                        "Syndrome extraction round latency for the QLDPC code (in nanoseconds)", 
                        memory_cycle_time_ns, 1300)

        .optional("-f", "--factory-physical-qubit-budget", "Number of physical qubits allocated to factory allocator", 
                      factory_physical_qubit_budget, 50000)
        .optional("", "--factory-ll-buffer-capacity", "Number of magic states stored in an last-level factory buffer",
                      factory_ll_buffer_capacity, 2)

        /*
         * These are parameters for analyzing *where* T bandwidth goes, since applications cannot saturate all of
         * it
         * */
        .optional("", "--bsol-elide-cliffords", "BW SoL: Elide Clifford gates", sim::GL_ELIDE_CLIFFORDS, false)
        .optional("", "--bsol-zero-latency-t", "BW SoL: Zero latency T gates", sim::GL_ZERO_LATENCY_T_GATES, false)

        .parse(argc, argv);

    if (GL_USE_RDR_ISA > 0)
    {
        sim::GL_RDR_ENABLED = true;
        std::cout << "RDR = enabled\n";
    }

    /* Parse trace string and do jit compilation if neeeded */

    auto traces = split_trace_string(trace_string);
    if (ratemode > 1 && traces.size() > 1)
        std::cerr << "cannot have multiple input traces if ratemode > 1" << _die{};

    if (jit)
        for (std::string& trace : traces)
            jit_compile(trace, inst_sim, compute_local_memory_capacity);

    if (ratemode > 1)
    {
        std::string trace{traces[0]};
        traces.resize(ratemode);
        std::fill(traces.begin(), traces.end(), trace);
    }

    // from `regime`, set parameters:
    const size_t compute_code_distance = get_compute_code_distance(regime),
                 memory_code_distance = get_memory_code_distance(regime);

    const size_t memory_block_physical_qubits = sim::configuration::bivariate_bicycle_code_physical_qubit_count(memory_code_distance);
    const size_t memory_block_capacity = sim::configuration::bivariate_bicycle_code_logical_qubit_count(memory_code_distance);

    /* initialize magic state factories */

    auto ms_specs = get_default_factory_specifications(regime, 
                                                        compute_cycle_time_ns, 
                                                        factory_ll_buffer_capacity);
    auto ms_alloc = sim::configuration::allocate_magic_state_factories(factory_physical_qubit_budget, ms_specs);

    /* initialize memory subsystem */

    // determine number of qubits for each trace:
    size_t main_memory_qubits = std::transform_reduce(traces.begin(), traces.end(), size_t{0},
                                                std::plus<size_t>{},
                                                [] (const std::string& t) { return get_number_of_qubits(t); });
    main_memory_qubits -= compute_local_memory_capacity;
    const double m_freq_khz = sim::compute_freq_khz(memory_cycle_time_ns);
    std::vector<sim::MEMORY_LEVEL*> memory_subsystem;
    if (main_memory_qubits > 0)
    {
        sim::MEMORY_LEVEL* bb_memory = new sim::BB_MEMORY(m_freq_khz, 
                                                            main_memory_qubits, 
                                                            memory_block_physical_qubits, 
                                                            memory_block_capacity, 
                                                            memory_code_distance);
        memory_subsystem.push_back(bb_memory);
    }

    /* initialize compute subsystem */

    double c_freq_khz = sim::compute_freq_khz(compute_cycle_time_ns);
    auto* compute_subsystem = new sim::COMPUTE_SUBSYSTEM(c_freq_khz, 
                                                         compute_code_distance,
                                                         compute_local_memory_capacity,
                                                         ms_alloc.producers.back(),
                                                         memory_subsystem);

    /* initialize driver */

    sim::DRIVER* driver = new sim::DRIVER(traces,
                                            concurrent_clients,
                                            inst_sim,
                                            compute_subsystem,
                                            ms_alloc.producers.back(),
                                            memory_subsystem);

    /* initialize simulation */

    std::vector<sim::OPERABLE*> all_operables{driver, compute_subsystem};
    std::copy(memory_subsystem.begin(), memory_subsystem.end(), std::back_inserter(all_operables));
    for (const auto& level : ms_alloc.producers)
        std::copy(level.begin(), level.end(), std::back_inserter(all_operables));
    sim::coordinate_clock_scale(all_operables);

    /* run simulation */

    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
    uint64_t last_print_cycle{0};
    do
    {
        if (print_progress > 0)
        {
            bool do_print = (driver->current_cycle() % print_progress == 0) 
                                && driver->current_cycle() > last_print_cycle;
            if (do_print)
            {
                driver->print_progress(std::cout);
                last_print_cycle = driver->current_cycle();
            }
        }
        
        for (auto* x : all_operables)
            x->tick();

        // check if we should do a skip:
        /*
        if (compute_subsystem->cycles_without_progress > skip_threshold)
        {
            auto skip = compute_subsystem->skip_to_cycle();
            if (skip.has_value() && compute_subsystem->current_cycle() < *skip)
            {
                uint64_t skip_time_ns = sim::convert_cycles_to_time_ns(*skip, compute_subsystem->freq_khz);
                sim::fast_forward_all_operables_to_time_ns(all_operables, skip_time_ns);
            }
        }
        */
    }
    while (!driver->done());
    driver->stop_simulation();

    /* print stats */

    sim::print_sim_stats(std::cout, driver);

//  sim::print_stats_for_factories(std::cout, "L1_FACTORY", ms_alloc.producers[0]);
//  sim::print_stats_for_factories(std::cout, "L2_FACTORY", ms_alloc.producers[1]);

//  print_stat_line(std::cout, "COMPUTE_CODE_DISTANCE", compute_code_distance);
//  print_stat_line(std::cout, "MEMORY_CODE_DISTANCE", memory_code_distance);

//  print_stat_line(std::cout, "COMPUTE_PHYSICAL_QUBITS", compute_physical_qubits);
//  print_stat_line(std::cout, "MEMORY_PHYSICAL_QUBITS", memory_physical_qubits);
//  print_stat_line(std::cout, "FACTORY_PHYSICAL_QUBITS", ms_alloc.physical_qubit_count);

//  if (use_remote_memory)
//      print_stat_line(std::cout, "ED_PHYSICAL_QUBITS", ed_alloc.physical_qubit_count);

    print_stat_line(std::cout, "T_BANDWIDTH_MAX_PER_S", ms_alloc.estimated_throughput);

    /*
    if (use_remote_memory)
    {
        uint64_t total_consumed_physical_epr_pairs = 
            std::transform_reduce(ed_alloc.producers[0].begin(), ed_alloc.producers[0].end(), uint64_t{0},
                                        std::plus<uint64_t>{},
                                        [] (const auto* _p)
                                        {
                                            const auto* p = static_cast<const sim::producer::ENT_DISTILLATION*>(_p);
                                            return p->s_physical_epr_pairs_consumed;
                                        });
        double physical_epr_bw = mean(total_consumed_physical_epr_pairs,
                                        compute_subsystem->current_cycle() / (1e3*compute_subsystem->freq_khz));
        print_stat_line(std::cout, "ED_BANDWIDTH_MAX_PER_S", ed_alloc.estimated_throughput);
        print_stat_line(std::cout, "PHYSICAL_EPR_PAIRS_CONSUMED", total_consumed_physical_epr_pairs);
        print_stat_line(std::cout, "PHYSICAL_EPR_BANDWIDTH", physical_epr_bw);
    }
    */

    print_stat_line(std::cout, "SIMULATION_WALLTIME_S", sim::walltime_s());

    /* Estimate logical error rate */

    /*
    for (auto* c : compute_subsystem->clients())
    {
        auto f = compute_application_fidelity(total_inst, c, compute_subsystem);
        std::cout << "CLIENT_" << static_cast<int>(c->id) << "_FIDELITY\n";
        print_stat_line(std::cout, "    OVERALL", f.overall);
        print_stat_line(std::cout, "    COMPUTE_SUBSYSTEM", f.compute_subsystem);
        print_stat_line(std::cout, "    MEMORY_SUBSYSTEM", f.memory_subsystem);
        print_stat_line(std::cout, "    MAGIC_STATE", f.magic_state);
    }
    */

    /* cleanup simulation */

    delete driver;
    delete compute_subsystem;
    for (auto* m : memory_subsystem)
        delete m;
    for (auto& p : ms_alloc.producers)
        for (auto* f : p)
            delete f;

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<std::string>
split_trace_string(std::string s)
{
    std::vector<std::string> result;
    size_t start = 0;
    size_t end = s.find(';');

    while (end != std::string::npos) {
        result.push_back(s.substr(start, end - start));
        start = end + 1;
        end = s.find(';', start);
    }

    // Add the last token (or the entire string if no semicolon was found)
    result.push_back(s.substr(start));

    return result;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
jit_compile(std::string& trace, int64_t inst_sim, int64_t active_set_capacity)
{
    constexpr auto MEMORY_ACCESS_SCHEDULER{compiler::pass::memory_scheduler::hint};

    std::string trace_dir = trace.substr(0, trace.find_last_of("/\\") + 1) + "jit/";
    std::string trace_filename = trace.substr(trace.find_last_of("/\\") + 1);

    mkdir(trace_dir.c_str(), 0777);

    auto ext_it = trace_filename.find(".gz");
    if (ext_it == std::string::npos)
        ext_it = trace_filename.find(".xz");
    std::string base_name = trace_filename.substr(0, ext_it);
    std::string active_set_capacity_str = std::to_string(active_set_capacity);
    std::string inst_str = std::to_string(inst_sim/1'000'000) + "M";

    std::string compiled_trace = trace_dir + base_name + "_a" + active_set_capacity_str + "_" + inst_str + ".gz";
    
    std::cout << "********* (jit) running memory access scheduler for " << trace 
                << " -> " << compiled_trace << " *********\n";

    generic_strm_type istrm, ostrm;
    generic_strm_open(istrm, trace, "rb");
    generic_strm_open(ostrm, compiled_trace, "wb");

    compiler::pass::memory_scheduler::config_type conf;
    conf.active_set_capacity = active_set_capacity;
    conf.inst_compile_limit = static_cast<int64_t>(5 * inst_sim);
    conf.print_progress_frequency = 0;
    conf.dag_inst_capacity = 100000;
    conf.hint_lookahead_depth = 256;

    run(ostrm, istrm, MEMORY_ACCESS_SCHEDULER, conf);

    generic_strm_close(istrm);
    generic_strm_close(ostrm);

    trace = compiled_trace;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
get_number_of_qubits(std::string_view trace)
{
    generic_strm_type istrm;
    generic_strm_open(istrm, std::string{trace}, "rb");
    uint32_t num_qubits;
    generic_strm_read(istrm, &num_qubits, 4);
    return num_qubits;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
get_compute_code_distance(std::string_view regime)
{
    if (regime == "T")
        return 23;
    else if (regime == "G")
        return 17;
    else if (regime == "M")
        return 11;
    else
        std::cerr << "get_compute_code_distance: unknown regime \"" << regime << "\"" << _die{};
}

size_t
get_memory_code_distance(std::string_view regime)
{
    if (regime == "T")
        return 24;
    else if (regime == "G")
        return 18;
    else if (regime == "M")
        return 12;
    else
        std::cerr << "get_compute_code_distance: unknown regime \"" << regime << "\"" << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<FACTORY_SPECIFICATION>
get_default_factory_specifications(std::string_view regime,
                                    int64_t c_round_time_ns,
                                    int64_t ll_buffer_capacity)
{
    FACTORY_SPECIFICATION l1_spec /* d = 3 color code cultivation */
    {
        .is_cultivation=true,
        .cycle_time_ns=c_round_time_ns,
        .buffer_capacity=1,
        .output_error_rate=1e-6,
        .escape_distance=13,
        .rounds=18,
        .probability_of_success=0.2
    };

    FACTORY_SPECIFICATION l2_spec /* 15:1, (dx,dz,dm) = (25,11,11) distillation */
    {
        .is_cultivation=false,
        .cycle_time_ns=c_round_time_ns,
        .buffer_capacity=ll_buffer_capacity,
        .output_error_rate=1e-12,
        .dx=25,
        .dz=11,
        .dm=11,
        .input_count=4,
        .output_count=1,
        .rotations=11
    };

    if (regime == "G")
    {
        // change parameters of d = 3 cultivation to d = 5 cultivation
        l1_spec.output_error_rate = 1e-8;
        l1_spec.escape_distance = 15;
        l1_spec.rounds = 25;
        l1_spec.probability_of_success = 0.02;
    }

    if (regime == "T")
        return {l1_spec, l2_spec};
    else
        return {l1_spec};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<ED_SPECIFICATION>
get_default_ed_specifications(std::string_view regime,
                                int64_t c_round_time_ns,
                                int64_t ll_buffer_capacity)
{
    /*
    auto specs = sim::configuration::ed::protocol_3(ll_buffer_capacity);
    for (auto& s : specs)
        s.cycle_time_ns = c_round_time_ns;
    return specs;
    */
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
faster_substrate_ed_overhead(ED_SPECIFICATION& s, int64_t substrate_mismatch_factor)
{
    /*
    // first, compute measurement distance for slower substrate
    const size_t slow_dm = sim::configuration::surface_code_distance_for_target_logical_error_rate(s.output_error_rate,
                                                                                                    sim::GL_PHYSICAL_ERROR_RATE);

    // use this to compute idle time
    const size_t idle_cycles = (s.input_count-s.output_count) * slow_dm * substrate_mismatch_factor;

    // there is no good analytical expression for the faster substrate code distance, but we know it is 
    // greater than or equal to `slow_dm`, so we can work from there
    size_t dm{slow_dm};
    auto f_error_rate = [idle_cycles, e_protocol=s.output_error_rate] (size_t d)
                        {
                            double ler = sim::configuration::surface_code_logical_error_rate(d, sim::GL_PHYSICAL_ERROR_RATE);
                            double log_idle_fidelity = mean(idle_cycles, d) * std::log(1-ler);
                            return 1 - (1-e_protocol)*std::exp(log_idle_fidelity);
                        };
    while (f_error_rate(dm) > 2*s.output_error_rate)
    {
        std::cout << "faster_substrate_ed_overhead: " << f_error_rate(dm) << " @ d = " << dm 
                    << ", need = " << s.output_error_rate
                    << "\t| idle_cycles = " << idle_cycles
                    << "\n";
        dm++;
    }

    double e = f_error_rate(dm);
    size_t idx = sim::configuration::inner_surface_code_distance_for_target_logical_error_rate(e, s.dx, sim::GL_PHYSICAL_ERROR_RATE);
    size_t idz = sim::configuration::inner_surface_code_distance_for_target_logical_error_rate(e, s.dz, sim::GL_PHYSICAL_ERROR_RATE);
    size_t p = sim::configuration::surface_code_physical_qubit_count(idx,idz) * s.input_count;
    p += p/2; // assume routing overheads add 50%

    std::cout << "faster_substrate_ed_overhead: [[ " << s.input_count 
                << ", " << s.output_count
                << ", dx=" << s.dx 
                << ", dz=" << s.dz 
                << " ]] uses inner codes with distance"
                << " dx = " << idx 
                << ", dz = " << idz
                << "\tphysical qubit overhead = " << p
                << "\n";
    
    return p;
    */
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

FIDELITY_RESULT
compute_application_fidelity(uint64_t scale_to_inst, sim::CLIENT* c, sim::COMPUTE_SUBSYSTEM* cs)
{
    /*
    std::vector<double> log_success_prob;

    // scale factor for all calculations
    const double scale = mean(scale_to_inst, c->s_unrolled_inst_done);

    // Compute subsystem contribution
    double sc_error_rate_per_d_cycles = sim::configuration::surface_code_logical_error_rate(cs->code_distance, sim::GL_PHYSICAL_ERROR_RATE);
    double cs_error_per_d_cycles = 1.0 - std::pow(1.0-sc_error_rate_per_d_cycles, cs->local_memory_capacity);
    double cs_scaled_cycles = scale * c->s_cycle_complete;
    double cs_log_success_prob = mean(cs_scaled_cycles, cs->code_distance) * std::log(1.0-cs_error_per_d_cycles);
    log_success_prob.push_back(cs_log_success_prob);

    // Memory subsystem contribution
    double memory_log_success_prob{0.0};
    for (const auto* s : cs->memory_hierarchy()->storages())
    {
        // compute final simulation cycle for client `c`
        auto final_cycle = sim::convert_cycles_between_frequencies(c->s_cycle_complete, cs->freq_khz, s->freq_khz);
        double error_rate_per_d_cycles = sim::configuration::bivariate_bicycle_code_block_error_rate(s->code_distance, sim::GL_PHYSICAL_ERROR_RATE);
        double scaled_cycles = scale * final_cycle;

        // also handle errors from memory accesses (the surgery + automorphism operations)
        double scaled_surgery_ops = s->s_surgery_operations * scale;
        double error_rate_per_surgery_op = (100+10) * error_rate_per_d_cycles; // 100x error is from surgery,
                                                                               // 10x is from automorphism

        double lgs = mean(scaled_cycles, s->code_distance) * std::log(1.0-error_rate_per_d_cycles) 
                     + scaled_surgery_ops * std::log(1.0-error_rate_per_surgery_op);
        memory_log_success_prob += lgs;
    }

    if (cs->is_ed_in_use())
    {
        // handle affects of entanglement distillation -- probability of teleportation failure:
        for (const auto* p : cs->entanglement_distillation_units().back())
            memory_log_success_prob += p->s_consumed * scale * std::log(1.0 - p->output_error_probability);
    }

    log_success_prob.push_back(memory_log_success_prob);

    // Magic state contribution
    const auto& f = cs->top_level_t_factories();
    double mean_t_error_probability = std::transform_reduce(f.begin(), f.end(), double{0.0}, std::plus<double>{},
                                                    [] (const auto* x) { return x->output_error_probability; }) / f.size();
    double scaled_t_count = scale * c->s_t_gates_done;
    double t_log_success_prob = scaled_t_count * std::log(1.0 - mean_t_error_probability);

    // finally, compute the probability that nothing fails using `log_success_prob`
    double log_fidelity = std::reduce(log_success_prob.begin(), log_success_prob.end(), 0.0);
    return FIDELITY_RESULT{
                std::exp(log_fidelity),  // total fidelity
                std::exp(cs_log_success_prob),
                std::exp(memory_log_success_prob),
                std::exp(t_log_success_prob)
            };

    */
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
