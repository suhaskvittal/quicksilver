/*
    author: Suhas Vittal
    date:   06 September 2025

    Unfortunately, quantum system simulation is a bit of a chicken-and-egg problem:
        (1) Input parameters determine the amount of time a program takes to run.
        (2) The time a program takes to run determines the probability of success.
        (3) The probability of success determines the input parameters.

    So, what do you do? Essentially, we want to have certain fixed variables, which
    in the case of this file, is the amount of memory and compute available to the 
    program. Given these fixed parameters, we will iterate until we land on a final
    configuration that has a decent probability of success.
*/

#include "argparse.h"
#include "compiler/memopt.h"
#include "sim.h"
#include "sim/utils/defs.h"
#include "sim/utils/estimation.h"
#include "sim/utils/factory_builder.h"

#include <sys/stat.h>

#include <zlib.h>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class INTA, class INTB> double
_fpdiv(INTA a, INTB b)
{
    return static_cast<double>(a) / static_cast<double>(b);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// want a 99% success rate for the application
const double TARGET_APP_SUCCESS_RATE{0.99};

struct ITERATION_CONFIG
{
    // target error rates are used to determine code distance and factory config 
    // -- probably the only thing that is modified between iterations
    double cmp_target_error_rate_per_cycle;
    double mem_bb_target_error_rate_per_cycle;
    double fact_target_error_rate_per_gate;

    // simulation setup:
    size_t      num_program_qubits;
    std::string trace;
    int64_t     inst_sim;
    int64_t     inst_assume_total;

    // compute setup:
    int64_t     cmp_sc_count;
    int64_t     cmp_sc_round_ns;

    // memory setup:
    size_t   mem_bb_num_modules;
    size_t   mem_bb_qubits_per_bank;
    int64_t  mem_bb_round_ns;
    bool     mem_is_remote;
    int64_t  mem_epr_buffer_capacity;
    int64_t  mem_mean_epr_generation_time_ns;

    // factory setup:
    int64_t fact_phys_qubits_per_program_qubit;

    // other:
    int64_t qht_latency_reduce_which;
    double  qht_reduction_fraction;

    // output of an iteration:
    double application_success_rate;
};

int64_t
qht_scale_round_ns(int64_t round_ns, double reduction_fraction)
{
    return static_cast<int64_t>(round_ns * (1-reduction_fraction));
}

ITERATION_CONFIG
sim_iteration(ITERATION_CONFIG conf, size_t sim_iter)
{
    constexpr size_t MIN_QUBITS{4};

    size_t num_program_qubits = conf.num_program_qubits;
    std::string trace = conf.trace;
    int64_t     inst_sim = conf.inst_sim;
    int64_t     inst_assume_total = conf.inst_assume_total;

    int64_t     cmp_sc_count = conf.cmp_sc_count;
    uint64_t    cmp_sc_round_ns = conf.cmp_sc_round_ns;

    size_t mem_bb_num_modules = conf.mem_bb_num_modules;
    size_t mem_bb_qubits_per_bank = conf.mem_bb_qubits_per_bank;
    uint64_t mem_bb_round_ns = conf.mem_bb_round_ns;
    bool     mem_is_remote = conf.mem_is_remote;
    int64_t  mem_epr_buffer_capacity = conf.mem_epr_buffer_capacity;
    int64_t  mem_mean_epr_generation_time_ns = conf.mem_mean_epr_generation_time_ns;

    int64_t fact_phys_qubits_per_program_qubit = conf.fact_phys_qubits_per_program_qubit;

    int64_t qht_latency_reduce_which = conf.qht_latency_reduce_which;
    double  qht_reduction_fraction = conf.qht_reduction_fraction;

    // 1. determine number of surface code qubits
    int64_t cmp_sc_adjusted_round_ns = (qht_latency_reduce_which & sim::QHT_LATENCY_REDUCTION_TARGET_COMPUTE)
                                            ? qht_scale_round_ns(cmp_sc_round_ns, qht_reduction_fraction)
                                            : cmp_sc_round_ns;
    size_t cmp_sc_code_distance = sim::est::sc_distance_for_target_logical_error_rate(conf.cmp_target_error_rate_per_cycle);
    double cmp_sc_freq_khz = sim::compute_freq_khz(cmp_sc_adjusted_round_ns, cmp_sc_code_distance);
    size_t cmp_sc_phys_qubits = cmp_sc_count * sim::est::sc_phys_qubit_count(cmp_sc_code_distance);

    size_t cmp_sc_num_patches_per_row = 4;
    size_t cmp_sc_num_rows = ceil(_fpdiv(cmp_sc_count, cmp_sc_num_patches_per_row));

    // 2. determine memory config:
    int64_t mem_bb_adjusted_round_ns = (qht_latency_reduce_which & sim::QHT_LATENCY_REDUCTION_TARGET_MEMORY)
                                            ? qht_scale_round_ns(mem_bb_round_ns, qht_reduction_fraction)
                                            : mem_bb_round_ns;
    size_t mem_bb_banks_per_module = (num_program_qubits > cmp_sc_count) 
                                    ? ceil(_fpdiv(num_program_qubits - cmp_sc_count, mem_bb_num_modules * mem_bb_qubits_per_bank)) 
                                    : 0;
    size_t mem_bb_code_distance = sim::est::mem_bb_distance_for_target_logical_error_rate(conf.mem_bb_target_error_rate_per_cycle);
    double mem_bb_freq_khz = sim::compute_freq_khz(mem_bb_adjusted_round_ns, mem_bb_code_distance);
    size_t mem_bb_phys_qubits = mem_bb_num_modules * mem_bb_banks_per_module * sim::est::bb_phys_qubit_count(mem_bb_code_distance);
    uint64_t mem_mean_epr_generation_cycle_time = sim::convert_ns_to_cycles(mem_mean_epr_generation_time_ns, mem_bb_freq_khz);

    // 2.0. create memory modules:
    std::vector<sim::MEMORY_MODULE*> mem_modules;
    for (size_t i = 0; i < mem_bb_num_modules; i++)
    {
        auto* m = new sim::MEMORY_MODULE(mem_bb_freq_khz, mem_bb_banks_per_module, mem_bb_qubits_per_bank,
                                        mem_is_remote, mem_epr_buffer_capacity, mem_mean_epr_generation_cycle_time);
        mem_modules.push_back(m);
    }

    // 2.1. create global EPR generator (if remote memory is enabled)
    if (mem_is_remote)
    {
        // Convert mean generation cycle time to frequency for EPR_GENERATOR
        double epr_freq_khz = mem_bb_freq_khz / mem_mean_epr_generation_cycle_time;
        // We'll pass the memory modules vector after creating them
        sim::GL_EPR = new sim::EPR_GENERATOR(epr_freq_khz, mem_modules, mem_epr_buffer_capacity);
    }

    // 3. determine factory config:
    int64_t l1_sc_round_ns = (qht_latency_reduce_which & sim::QHT_LATENCY_REDUCTION_TARGET_ALL_FACTORY)
                                ? qht_scale_round_ns(cmp_sc_round_ns, qht_reduction_fraction)
                                : cmp_sc_round_ns;
    int64_t l2_sc_round_ns = (qht_latency_reduce_which & sim::QHT_LATENCY_REDUCTION_TARGET_ALL_FACTORY)
                                ? qht_scale_round_ns(cmp_sc_round_ns, qht_reduction_fraction)
                                : cmp_sc_round_ns;

    size_t fact_max_phys_qubits = num_program_qubits * fact_phys_qubits_per_program_qubit;
    auto [t_factories, fact_phys_qubits, factory_conf] = sim::util::factory_build(conf.fact_target_error_rate_per_gate,
                                                                        fact_max_phys_qubits,
                                                                        l1_sc_round_ns,
                                                                        l2_sc_round_ns);
    double fact_l1_freq_khz, fact_l2_freq_khz;
    size_t fact_l1_count{0}, fact_l2_count{0};
    for (auto* f : t_factories)
    {
        if (f->level_ == 0)
        {
            fact_l1_count++;
            fact_l1_freq_khz = f->OP_freq_khz;
        }
        else
        {
            fact_l2_count++;
            fact_l2_freq_khz = f->OP_freq_khz;
        }
    }

    // 4. initialize `GL_CMP`
    sim::GL_CMP = new sim::COMPUTE(cmp_sc_freq_khz, {trace}, cmp_sc_num_rows, cmp_sc_num_patches_per_row, t_factories, mem_modules);

    // 5. run simulation:
    std::cout << "------------- SIM ITERATION " << sim_iter << " -------------\n";
    sim::print_stat_line(std::cout, "CMP_CODE_DISTANCE", cmp_sc_code_distance, false);
    sim::print_stat_line(std::cout, "CMP_COMPUTE_PATCHES", cmp_sc_num_patches_per_row*cmp_sc_num_rows, false);
    sim::print_stat_line(std::cout, "CMP_FREQUENCY_KHZ", cmp_sc_freq_khz, false);

    sim::print_stat_line(std::cout, "FACT_L1_COUNT", fact_l1_count, false);
    sim::print_stat_line(std::cout, "FACT_L1_TYPE", factory_conf[0].which, false);
    sim::print_stat_line(std::cout, "FACT_L1_FREQUENCY_KHZ", fact_l1_freq_khz, false);
    sim::print_stat_line(std::cout, "FACT_L2_COUNT", fact_l2_count, false);
    if (factory_conf.size() > 1)
    {
        sim::print_stat_line(std::cout, "FACT_L2_TYPE", factory_conf[1].which, false);
        sim::print_stat_line(std::cout, "FACT_L2_FREQUENCY_KHZ", fact_l2_freq_khz, false);
    }
    else
    {
        sim::print_stat_line(std::cout, "FACT_L2_TYPE", "NONE", false);
        sim::print_stat_line(std::cout, "FACT_L2_FREQUENCY_KHZ", 0, false);
    }

    sim::print_stat_line(std::cout, "MEM_BB_CODE_DISTANCE", mem_bb_code_distance, false);
    sim::print_stat_line(std::cout, "MEM_BB_BANKS_PER_MODULE", mem_bb_banks_per_module, false);
    sim::print_stat_line(std::cout, "MEM_BB_FREQUENCY_KHZ", mem_bb_freq_khz, false);
    if (mem_is_remote)
        sim::print_stat_line(std::cout, "MEM_BB_MEAN_EPR_GENERATION_CYCLE_TIME", mem_mean_epr_generation_cycle_time, false);

    // reset global variables
    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
    sim::GL_CURRENT_TIME_NS = 0;

    // 5.2. initialize each component:
    sim::GL_CMP->OP_init();
    for (auto* m : mem_modules)
        m->OP_init();
    for (auto* f : t_factories)
        f->OP_init();
    if (sim::GL_EPR != nullptr)
        sim::GL_EPR->OP_init();

    // 5.3. loop until `inst_sim` is reached
    bool done;
    do
    {
        // arbitrate events:
        sim::T_FACTORY* earliest_fact = sim::arbitrate_event_selection_from_vector(t_factories);
        sim::MEMORY_MODULE* earliest_mem = sim::arbitrate_event_selection_from_vector(mem_modules);

        bool deadlock;
        if (sim::GL_EPR == nullptr)
            deadlock = sim::arbitrate_event_execution(earliest_fact, earliest_mem, sim::GL_CMP);
        else
            deadlock = sim::arbitrate_event_execution(earliest_fact, earliest_mem, sim::GL_CMP, sim::GL_EPR);

        if (deadlock)
        {
            sim::GL_CMP->dump_deadlock_info();

            for (size_t i = 0; i < mem_modules.size(); i++)
            {
                std::cerr << "memory module " << i 
                            << " (patch = " << mem_modules[i]->output_patch_idx_  
                            << ")------------------\n";
                mem_modules[i]->dump_contents();
            }
    
            std::vector<sim::T_FACTORY*> l1_factories, l2_factories;
            std::copy_if(t_factories.begin(), t_factories.end(), std::back_inserter(l1_factories),
                        [] (auto* f) { return f->level_ == 0; });
            std::copy_if(t_factories.begin(), t_factories.end(), std::back_inserter(l2_factories),
                        [] (auto* f) { return f->level_ == 1; });

            std::cerr << "L1 factories:\n";
            for (auto* f : l1_factories)
                std::cerr << "\tbuffer occu = " << f->get_occupancy() << ", step = " << f->get_step() << "\n";
            std::cerr << "L2 factories:\n";
            for (auto* f : l2_factories)
                std::cerr << "\tbuffer occu = " << f->get_occupancy() << ", step = " << f->get_step() << "\n";

            std::cerr << "EPR:\n";
            sim::GL_EPR->dump_deadlock_info();

            throw std::runtime_error("deadlock detected");
        }

        // check if we are done:
        const auto& clients = sim::GL_CMP->get_clients();
        done = std::all_of(clients.begin(), clients.end(),
                            [inst_sim] (const auto& c) { return c->s_unrolled_inst_done >= inst_sim; });
    }
    while (!done);

    // 6. compute application success rate and update target error rates
    double inst_sim_ratio = _fpdiv(inst_assume_total, inst_sim);
    // 6.1. compute error:
    double cmp_cycles = static_cast<double>(sim::GL_CMP->current_cycle());
    cmp_cycles *= inst_sim_ratio;  // scale up `cmp_cycles`
    double cmp_error_rate_per_cycle_one_qubit = sim::est::sc_logical_error_rate(cmp_sc_code_distance);
    double cmp_error_rate_per_cycle_all_qubits = cmp_error_rate_per_cycle_one_qubit * cmp_sc_count;
    double cmp_total_error = cmp_error_rate_per_cycle_all_qubits * cmp_cycles;

    // 6.2. memory error:
    double mem_cycles = static_cast<double>(mem_modules[0]->current_cycle());
    mem_cycles *= inst_sim_ratio;  // scale up `mem_cycles`
    double mem_error_rate_per_cycle_one_block = sim::est::mem_bb_logical_error_rate(mem_bb_code_distance);
    double mem_error_rate_per_cycle_all_blocks = mem_error_rate_per_cycle_one_block * mem_bb_num_modules * mem_bb_banks_per_module;
    double mem_total_error = mem_error_rate_per_cycle_all_blocks * mem_cycles;

    // 6.3. factory error:
    const auto& client = sim::GL_CMP->get_clients()[0];
    double total_t_gates = static_cast<double>(client->s_t_gate_count) * inst_sim_ratio;
    double fact_total_error = client->s_total_t_error * inst_sim_ratio;

    // 6.4. application success rate:
    double application_success_rate = 1.0 - cmp_total_error - mem_total_error - fact_total_error;

    // 6.5. update configuration
    conf.cmp_target_error_rate_per_cycle = (1.0 - TARGET_APP_SUCCESS_RATE) / (cmp_cycles * cmp_sc_count);
    conf.mem_bb_target_error_rate_per_cycle = (1.0 - TARGET_APP_SUCCESS_RATE) / (mem_cycles * mem_bb_num_modules * mem_bb_banks_per_module);
    conf.fact_target_error_rate_per_gate = (1.0 - TARGET_APP_SUCCESS_RATE) / total_t_gates;

    // 7. print stats:
    sim::print_stats(std::cout);
    sim::print_stat_line(std::cout, "SCALED_CMP_CYCLES", cmp_cycles, false);
    sim::print_stat_line(std::cout, "L1_FACTORY_COUNT", fact_l1_count, false);
    sim::print_stat_line(std::cout, "L2_FACTORY_COUNT", fact_l2_count, false);
    sim::print_stat_line(std::cout, "COMPUTE_TOTAL_PHYSICAL_QUBITS", cmp_sc_phys_qubits, false);
    sim::print_stat_line(std::cout, "FACTORY_TOTAL_PHYSICAL_QUBITS", fact_phys_qubits, false);
    sim::print_stat_line(std::cout, "MEMORY_TOTAL_PHYSICAL_QUBITS", mem_bb_phys_qubits, false);
    sim::print_stat_line(std::cout, "SIMULATED_CODE_DISTANCE", cmp_sc_code_distance, false);
    sim::print_stat_line(std::cout, "COMPUTE_TOTAL_ERROR", cmp_total_error, false);
    sim::print_stat_line(std::cout, "MEMORY_TOTAL_ERROR", mem_total_error, false);
    sim::print_stat_line(std::cout, "FACTORY_TOTAL_ERROR", fact_total_error, false);
    sim::print_stat_line(std::cout, "APPLICATION_SUCCESS_RATE", application_success_rate, false);

    std::cout << "NEXT_ITERATION\n";
    sim::print_stat_line(std::cout, "CMP_TARGET_ERROR_RATE_PER_CYCLE", conf.cmp_target_error_rate_per_cycle);
    sim::print_stat_line(std::cout, "MEM_BB_TARGET_ERROR_RATE_PER_CYCLE", conf.mem_bb_target_error_rate_per_cycle);
    sim::print_stat_line(std::cout, "FACT_TARGET_ERROR_RATE_PER_GATE", conf.fact_target_error_rate_per_gate);

    // 8. dealloc memory:
    delete sim::GL_CMP;
    for (auto* m : mem_modules)
        delete m;
    for (auto* f : t_factories)
        delete f;
    if (sim::GL_EPR != nullptr)
        delete sim::GL_EPR;

    return conf;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string trace;

    int64_t inst_sim;
    int64_t inst_assume_total;

    double initial_target_error_rate;

    bool jit;
    
    // compute budget can be allocated absolutely (`cmp_num_surface_codes`) or as a proportion of the total number of program qubits (`cmp_surface_code_fraction`)
    int64_t cmp_sc_count;
    int64_t cmp_sc_round_ns;

    int64_t fact_phys_qubits_per_program_qubit;

    // memory budget can be allocated absolutely (`mem_num_modules`) or as a proportion of the total number of program qubits (`mem_fraction_budget`)
    int64_t mem_bb_num_modules;
    int64_t mem_bb_qubits_per_bank;
    int64_t mem_bb_round_ns;
    bool    mem_is_remote;
    int64_t mem_epr_buffer_capacity;
    double  mem_epr_generation_frequency_khz;

    int64_t qht_latency_reduce_which;
    double  qht_reduction_fraction;

    ARGPARSE()
        .required("trace", "path to trace file", trace)
        .required("inst-sim", "number of instructions to simulate", inst_sim)
        .required("inst-assume-total", "number of instructions assumed to be in the larger program", inst_assume_total)

        // simulator verbosity:
        .optional("-p", "--print-progress", "print progress frequency", sim::GL_PRINT_PROGRESS_FREQ, -1)

        // just in time compilation (limited qubit count):
        .optional("-jit", "--just-in-time-compilation", "enable just in time compilation for limited qubit count", jit, false)

        // setup:
        .optional("-ems", "--elide-mswap-instructions", "elide mswap instructions", sim::GL_ELIDE_MSWAP_INSTRUCTIONS, false)
        .optional("-e", "--initial-target-error-rate", "initial target error rate", initial_target_error_rate, 1e-12)
        
        // configuration:
        .optional("", "--cmp-sc-count", "number of surface codes to allocate to compute", cmp_sc_count, 4)
        .optional("", "--cmp-sc-round-ns", "round time for surface code", cmp_sc_round_ns, 1200)

        .optional("", "--fact-phys-qubits-per-program-qubit", "number of physical qubits to allocate to factories", 
                    fact_phys_qubits_per_program_qubit, 50)
        .optional("-cult", "--cultivation", "prefer cultivation over distillation", sim::GL_PREF_CULTIVATION, false)

        .optional("", "--mem-bb-num-modules", "number of memory banks per module", mem_bb_num_modules, 2)
        .optional("", "--mem-bb-qubits-per-bank", "number of qubits per bank", mem_bb_qubits_per_bank, 12)
        .optional("", "--mem-bb-round-ns", "round time for memory banks", mem_bb_round_ns, 1300)
        .optional("", "--mem-is-remote", "enable remote memory", mem_is_remote, false)
        .optional("", "--mem-epr-buffer-capacity", "remote memory epr buffer capacity", mem_epr_buffer_capacity, 16)
        .optional("", "--mem-epr-generation-frequency", "remote memory epr generation frequency (in kHz)", 
                    mem_epr_generation_frequency_khz, 0.012)

        // architectural policies:
        .optional("-cs", "--impl-cacheable-stores", "enable cacheable stores", sim::GL_IMPL_CACHEABLE_STORES, false)

        // quantum hyperthreading parameters:
        .optional("-qht", "--qht-reduce-which", "QHT latency reduction target", qht_latency_reduce_which, 0)
        .optional("", "--qht-reduction-fraction", "fraction of syndrome extraction time to reduce", qht_reduction_fraction, 0.2)
        .parse(argc, argv);

    sim::GL_PRINT_PROGRESS = (sim::GL_PRINT_PROGRESS_FREQ > 0);

    uint64_t mem_mean_epr_generation_time_ns = (1.0/mem_epr_generation_frequency_khz) * 1e6;
    
    // if `jit`, then `trace` is just the base version -- we need to create a new trace with the specified `cmp_sc_count`
    if (jit)
    {
        // Create new trace filename with instruction count
        std::string trace_dir = trace.substr(0, trace.find_last_of("/\\") + 1) + "jit/";
        std::string trace_filename = trace.substr(trace.find_last_of("/\\") + 1);

        mkdir(trace_dir.c_str(), 0777);
        
        std::string new_trace;
        auto it = trace_filename.find(".gz");
        if (it == std::string::npos)
            it = trace_filename.find(".xz");
        if (it == std::string::npos)
            throw std::runtime_error("trace file must be .gz or .bin");

        std::string base_name = trace_filename.substr(0, it);
        std::string file_ext = trace_filename.substr(it);
        
        std::string cmp_sc_count_str = std::to_string(cmp_sc_count);
        std::string inst_str = std::to_string(inst_sim/1'000'000) + "M";
        new_trace = trace_dir + base_name + "_c" + cmp_sc_count_str + "_" + inst_str + ".gz";

        std::cout << "****** (jit) running memory compiler for " << trace << " -> " << new_trace << " *******\n";

        // do mem compile for the new trace:
        generic_strm_type istrm, ostrm;

        generic_strm_open(istrm, trace, "rb");
        generic_strm_open(ostrm, new_trace, "wb");
        MEMOPT mc(cmp_sc_count, MEMOPT::EMIT_IMPL_ID::HINT_SIMPLE, sim::GL_PRINT_PROGRESS_FREQ);
//      MEMOPT mc(cmp_sc_count, MEMOPT::EMIT_IMPL_ID::VISZLAI, sim::GL_PRINT_PROGRESS_FREQ);
        mc.run(istrm, ostrm, 2*inst_sim);
        generic_strm_close(istrm);
        generic_strm_close(ostrm);

        std::cout << "****** (jit) memory compiler done *******\n";

        // Update trace to use the new filename
        trace = new_trace;
    }

    // read trace to identify number of program qubits:
    uint32_t num_program_qubits{};
    generic_strm_type istrm;
    generic_strm_open(istrm, trace, "rb");
    generic_strm_read(istrm, &num_program_qubits, 4);
    generic_strm_close(istrm);

    // start sim loop:
    ITERATION_CONFIG conf;

    // initialize config:
    conf.cmp_target_error_rate_per_cycle = initial_target_error_rate;
    conf.mem_bb_target_error_rate_per_cycle = initial_target_error_rate;
    conf.fact_target_error_rate_per_gate = initial_target_error_rate;

    conf.num_program_qubits = num_program_qubits;
    conf.trace = trace;
    conf.inst_sim = inst_sim;
    conf.inst_assume_total = inst_assume_total;

    conf.cmp_sc_count = cmp_sc_count;
    conf.cmp_sc_round_ns = cmp_sc_round_ns;

    conf.mem_bb_num_modules = mem_bb_num_modules;
    conf.mem_bb_qubits_per_bank = mem_bb_qubits_per_bank;
    conf.mem_bb_round_ns = mem_bb_round_ns;
    conf.mem_is_remote = mem_is_remote;
    conf.mem_epr_buffer_capacity = mem_epr_buffer_capacity;
    conf.mem_mean_epr_generation_time_ns = mem_mean_epr_generation_time_ns;

    conf.fact_phys_qubits_per_program_qubit = fact_phys_qubits_per_program_qubit;

    conf.qht_latency_reduce_which = qht_latency_reduce_which;
    conf.qht_reduction_fraction = qht_reduction_fraction;

    size_t sim_iter{0};
    bool converged{false};
//  while (!converged)
    while (sim_iter < 1)
    {
        conf = sim_iteration(conf, sim_iter);
        sim_iter++;
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
