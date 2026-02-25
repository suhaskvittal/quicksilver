/*
 *  author: Suhas Vittal
 *  date:   16 January 2026
 * */

#include "sim.h"
#include "sim/configuration/allocator/impl.h"
#include "sim/configuration/predefined_ed_protocols.h"
#include "sim/configuration/resource_estimation.h"
#include "sim/compute_subsystem.h"
#include "sim/memory/remote.h"
#include "sim/memory_subsystem.h"
#include "sim/production/magic_state.h"

#include "compiler/memory_scheduler.h"
#include "compiler/memory_scheduler/impl.h"

#include "argparse.h"

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
                                                                      int64_t compute_syndrome_extraction_round_time_ns,
                                                                      int64_t ll_buffer_capacity);
std::vector<ED_SPECIFICATION>      get_default_ed_specifications(std::string_view regime,
                                                                    int64_t compute_syndrome_extraction_round_time_ns,
                                                                    int64_t ll_buffer_capacity);

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
    std::string trace_string;
    int64_t     inst_sim;

    int64_t print_progress;
    int64_t ratemode;
    int64_t skip_threshold;
    bool    jit;
    std::string regime;

    int64_t concurrent_clients;
    int64_t compute_local_memory_capacity;
    int64_t compute_syndrome_extraction_round_time_ns;

    int64_t memory_syndrome_extraction_round_time_ns;
    bool    use_remote_memory;
    int64_t epr_physical_qubit_budget;
    int64_t epr_ll_buffer_capacity;

    int64_t factory_ll_buffer_capacity;
    int64_t factory_physical_qubit_budget;

    sim::compute_extended_config conf;

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

        .optional("-c", "--concurrent-clients", "Number of active concurrent clients", concurrent_clients, 1)
        .optional("-a", "--compute-local-memory-capacity", "Number of active qubits in the compute subsystem's local memory", 
                      compute_local_memory_capacity, 12)
        .optional("", "--compute-syndrome-extraction-round-time-ns", 
                      "Syndrome extraction round latency for surface code (in nanoseconds)", 
                      compute_syndrome_extraction_round_time_ns, 1200)

        .optional("-ttpl", "--t-teleport-limit", 
                        "Max number of T gate teleportations after initial T gate", 
                        sim::GL_T_GATE_TELEPORTATION_MAX, 0)
        .optional("", "--enable-t-autocorrect", 
                        "Use auto correction when applying T gates", sim::GL_T_GATE_DO_AUTOCORRECT, false)

        .optional("-rpc", "--rpc", "Enable rotation precomputation", conf.rpc_enabled, false)
        .optional("", "--rpc-ttp-always", "Enable T teleportation always for rotation subsystem", sim::GL_RPC_ALWAYS_USE_TELEPORTATION, false)
        .optional("", "--rpc-capacity", "Amount of rotation precomputation storage", conf.rpc_capacity, 2)
        .optional("", "--rpc-watermark", "Watermark for rotation precomputation", conf.rpc_watermark, 0.5)
        .optional("", "--rpc-always-runahead", "Always runahead (even on rotation success)", sim::GL_RPC_ALWAYS_RUNAHEAD, false)
        .optional("", "--rpc-inst-delta-limit", "Instruction delta limit for runahead", sim::GL_RPC_INST_DELTA_LIMIT, 100000)
        .optional("", "--rpc-degree", "Runahead degree of RPC (number of runahead instructions on trigger)", sim::GL_RPC_DEGREE, 4)

        .optional("", "--memory-syndrome-extraction-round-time-ns", 
                        "Syndrome extraction round latency for the QLDPC code (in nanoseconds)", 
                        memory_syndrome_extraction_round_time_ns, 1300)
        .optional("", "--memory-is-remote", 
                        "Storages in memory subsystem consume EPR pairs", 
                        use_remote_memory, false)
        .optional("-epr", "--epr-physical-qubit-budget", 
                        "Physical qubit budget for ED used by remote storage", 
                        epr_physical_qubit_budget, 5000)
        .optional("", "--epr-ll-buffer-capacity",
                        "Number of EPR pairs stored in a last-level ED buffer",
                        epr_ll_buffer_capacity, 4)

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

    GL_USE_RPC_ISA = 1;

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
                 memory_code_distance = 18;//get_memory_code_distance(regime);

    const size_t memory_block_physical_qubits = sim::configuration::bivariate_bicycle_code_physical_qubit_count(memory_code_distance);
    const size_t memory_block_capacity = sim::configuration::bivariate_bicycle_code_logical_qubit_count(memory_code_distance);

    /* initialize magic state factories */

    auto ms_specs = get_default_factory_specifications(regime, 
                                                        compute_syndrome_extraction_round_time_ns, 
                                                        factory_ll_buffer_capacity);
    auto ms_alloc = sim::configuration::allocate_magic_state_factories(factory_physical_qubit_budget, ms_specs);

    /* initialize memory subsystem */

    // if `use_remote_memory` is set, then initialize `ed_alloc`
    sim::configuration::ALLOCATION ed_alloc;
    if (use_remote_memory)
    {
        int64_t scale_factor = memory_syndrome_extraction_round_time_ns / 1300;
        auto ed_specs = get_default_ed_specifications(regime,
                                                        compute_syndrome_extraction_round_time_ns*scale_factor,
                                                        epr_ll_buffer_capacity);
        ed_alloc = sim::configuration::allocate_entanglement_distillation_units(epr_physical_qubit_budget, ed_specs);
        conf.ed_units = ed_alloc.producers;
    }

    // determine number of qubits for each trace:
    size_t main_memory_qubits = std::transform_reduce(traces.begin(), traces.end(), size_t{0},
                                                std::plus<size_t>{},
                                                [] (const std::string& t) { return get_number_of_qubits(t); });
    main_memory_qubits -= compute_local_memory_capacity;
    const size_t num_blocks = main_memory_qubits == 0 ? 0 : (main_memory_qubits-1) / memory_block_capacity + 1;
    const double m_freq_khz = sim::compute_freq_khz(memory_syndrome_extraction_round_time_ns);
    std::vector<sim::STORAGE*> memory_blocks(num_blocks);
    for (size_t i = 0; i < num_blocks; i++)
    {
        sim::STORAGE* m;
        if (use_remote_memory)
        {
            m = new sim::REMOTE_STORAGE(m_freq_khz,
                                        memory_block_physical_qubits,
                                        memory_block_capacity,
                                        memory_code_distance,
                                        1,
                                        2*memory_code_distance,
                                        1*memory_code_distance,
                                        ed_alloc.producers.back());
        }
        else
        {
           m = new sim::STORAGE(m_freq_khz, 
                                memory_block_physical_qubits,
                                memory_block_capacity,
                                memory_code_distance,
                                1, // num adapters
                                2*memory_code_distance, // load latency
                                1*memory_code_distance); // store latency
        }
        memory_blocks[i] = m;
    }
    sim::MEMORY_SUBSYSTEM* memory_subsystem = new sim::MEMORY_SUBSYSTEM(std::move(memory_blocks));

    /* initialize compute subsystem */

    double c_freq_khz;
    c_freq_khz = sim::compute_freq_khz(compute_syndrome_extraction_round_time_ns);
    auto* compute_subsystem = new sim::COMPUTE_SUBSYSTEM{c_freq_khz,
                                                         traces,
                                                         compute_code_distance,
                                                         compute_local_memory_capacity,
                                                         concurrent_clients,
                                                         inst_sim,
                                                         ms_alloc.producers.back(),
                                                         memory_subsystem,
                                                         conf};
    assert(compute_subsystem->is_ed_in_use() == use_remote_memory);

    /* initialize simulation */

    std::vector<sim::OPERABLE*> all_operables;
    all_operables.push_back(compute_subsystem);
    std::copy(memory_subsystem->storages().begin(), memory_subsystem->storages().end(), std::back_inserter(all_operables));
    for (const auto& level : ms_alloc.producers)
        std::copy(level.begin(), level.end(), std::back_inserter(all_operables));

    if (use_remote_memory)
        for (const auto& level : ed_alloc.producers)
            std::copy(level.begin(), level.end(), std::back_inserter(all_operables));

    if (compute_subsystem->is_rpc_enabled())
        all_operables.push_back(compute_subsystem->rotation_subsystem());

    sim::coordinate_clock_scale(all_operables);

    std::cout << "simulation parameters:"
                << "\n\tqubits in local memory = " << compute_local_memory_capacity
                << "\n\tqubits in main memory (blocks) = " << main_memory_qubits << " (" << num_blocks << ")";
    for (size_t i = 0; i < ms_alloc.producers.size(); i++)
        std::cout << "\n\tL" << i+1 << " factory count = " << ms_alloc.producers[i].size();
    if (use_remote_memory)
        for (size_t i = 0; i < ed_alloc.producers.size(); i++)
            std::cout << "\n\tL" << i+1 << " ed unit count = " << ed_alloc.producers[i].size();
    std::cout << "\n";

    /* run simulation */

    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
    uint64_t last_print_cycle{0};
    do
    {
        if (print_progress > 0)
        {
            bool do_print = (compute_subsystem->current_cycle() % print_progress == 0)
                            && compute_subsystem->current_cycle() > last_print_cycle;
            if (do_print)
            {
                compute_subsystem->print_progress(std::cout);
                last_print_cycle = compute_subsystem->current_cycle();
            }
        }
        
        for (auto* x : all_operables)
            x->tick();

        // check if we should do a skip:
        if (compute_subsystem->cycles_without_progress > skip_threshold)
        {
            auto skip = compute_subsystem->skip_to_cycle();
            if (skip.has_value() && compute_subsystem->current_cycle() < *skip)
            {
                uint64_t skip_time_ns = sim::convert_cycles_to_time_ns(*skip, compute_subsystem->freq_khz);
                sim::fast_forward_all_operables_to_time_ns(all_operables, skip_time_ns);
            }
        }
    }
    while (!compute_subsystem->done());

    /* print stats */

    size_t compute_physical_qubits = sim::configuration::surface_code_physical_qubit_count(compute_code_distance)
                                            * compute_local_memory_capacity;
    size_t memory_physical_qubits = std::transform_reduce(memory_subsystem->storages().begin(),
                                                            memory_subsystem->storages().end(),
                                                            size_t{0},
                                                            std::plus<size_t>{},
                                                            [] (auto* s) { return s->physical_qubit_count; });

    sim::print_compute_subsystem_stats(std::cout, compute_subsystem);

    sim::print_stats_for_factories(std::cout, "L1_FACTORY", ms_alloc.producers[0]);
    sim::print_stats_for_factories(std::cout, "L2_FACTORY", ms_alloc.producers[1]);

    print_stat_line(std::cout, "COMPUTE_PHYSICAL_QUBITS", compute_physical_qubits);
    print_stat_line(std::cout, "MEMORY_PHYSICAL_QUBITS", memory_physical_qubits);
    print_stat_line(std::cout, "FACTORY_PHYSICAL_QUBITS", ms_alloc.physical_qubit_count);

    if (use_remote_memory)
        print_stat_line(std::cout, "ED_PHYSICAL_QUBITS_PER_SIDE", ed_alloc.physical_qubit_count);

    print_stat_line(std::cout, "T_BANDWIDTH_MAX_PER_S", ms_alloc.estimated_throughput);

    if (use_remote_memory)
        print_stat_line(std::cout, "ED_BANDWIDTH_MAX_PER_S", ed_alloc.estimated_throughput);

    print_stat_line(std::cout, "SIMULATION_WALLTIME_S", sim::walltime_s());

    /* Estimate logical error rate */

    for (auto* c : compute_subsystem->clients())
    {
        auto f = compute_application_fidelity(1'000'000'000, c, compute_subsystem);
        std::cout << "CLIENT_" << static_cast<int>(c->id) << "_FIDELITY\n";
        print_stat_line(std::cout, "    OVERALL", f.overall);
        print_stat_line(std::cout, "    COMPUTE_SUBSYSTEM", f.compute_subsystem);
        print_stat_line(std::cout, "    MEMORY_SUBSYSTEM", f.memory_subsystem);
        print_stat_line(std::cout, "    MAGIC_STATE", f.magic_state);
    }

    /* cleanup simulation */

    delete compute_subsystem;
    delete memory_subsystem;
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
    constexpr auto MEMORY_ACCESS_SCHEDULER{compile::memory_scheduler::hint};

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

    compile::memory_scheduler::config_type conf;
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
        .syndrome_extraction_round_time_ns=c_round_time_ns,
        .buffer_capacity=1,
        .output_error_rate=1e-6,
        .escape_distance=13,
        .rounds=18,
        .probability_of_success=0.2
    };

    FACTORY_SPECIFICATION l2_spec /* 15:1, (dx,dz,dm) = (25,11,11) distillation */
    {
        .is_cultivation=false,
        .syndrome_extraction_round_time_ns=c_round_time_ns,
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
    return sim::configuration::ed::protocol_3(c_round_time_ns, ll_buffer_capacity);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

FIDELITY_RESULT
compute_application_fidelity(uint64_t scale_to_inst, sim::CLIENT* c, sim::COMPUTE_SUBSYSTEM* cs)
{
    std::vector<double> log_success_prob;

    // scale factor for all calculations
    const double scale = mean(scale_to_inst, c->s_unrolled_inst_done);

    /* Compute subsystem contribution */
    double sc_error_rate_per_d_cycles = sim::configuration::surface_code_logical_error_rate(cs->code_distance, sim::GL_PHYSICAL_ERROR_RATE);
    double cs_error_per_d_cycles = 1.0 - std::pow(1.0-sc_error_rate_per_d_cycles, cs->local_memory_capacity);
    double cs_scaled_cycles = scale * c->s_cycle_complete;
    double cs_log_success_prob = mean(cs_scaled_cycles, cs->code_distance) * std::log(1.0-cs_error_per_d_cycles);
    log_success_prob.push_back(cs_log_success_prob);

    /* Memory subsystem contribution */
    double memory_log_success_prob{0.0};
    for (const auto* s : cs->memory_hierarchy()->storages())
    {
        // compute final simulation cycle for client `c`
        auto final_cycle = sim::convert_cycles_between_frequencies(c->s_cycle_complete, cs->freq_khz, s->freq_khz);
        double error_rate_per_d_cycles = sim::configuration::bivariate_bicycle_code_block_error_rate(s->code_distance, sim::GL_PHYSICAL_ERROR_RATE);
        double scaled_cycles = scale * final_cycle;
        double lgs = mean(scaled_cycles, s->code_distance) * std::log(1.0-error_rate_per_d_cycles);
        memory_log_success_prob += lgs;
    }

    if (cs->is_ed_in_use())
    {
        // handle affects of entanglement distillation -- probability of teleportation failure:
        for (const auto* p : cs->entanglement_distillation_units().back())
            memory_log_success_prob += p->s_consumed * scale * std::log(1.0 - p->output_error_probability);
    }

    log_success_prob.push_back(memory_log_success_prob);

    /* Magic state contribution */
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
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
