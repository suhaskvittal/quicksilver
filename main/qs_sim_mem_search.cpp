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

size_t
sc_phys_qubit_count(size_t d)
{
    return 2*d*d - 1;
}

size_t
sc_phys_qubit_count(size_t dx, size_t dz)
{
    return 2*dx*dz - 1;
}

size_t
bb_phys_qubit_count(size_t d)
{
    return 2 * 72 * (d/6);
}

size_t
fact_logical_qubit_count(std::string which)
{
    // this includes the ancillary space required to perform pauli-product rotations
    if (which == "15to1")
        return 9;
    else if (which == "20to4")
        return 12;
    else
        throw std::runtime_error("fact_logical_qubit_count: unknown logical qubit count for " + which);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const double PHYS_ERROR{1e-3};

double
sc_logical_error_rate(size_t d)
{
    return 0.1 * pow(100*PHYS_ERROR, 0.5*(d+1));
}

size_t
sc_distance_for_target_logical_error_rate(double e)
{
    double d = 2.0 * ((log(e) - log(0.1)) / log(100*PHYS_ERROR)) - 1.0;

    // need to intelligently round while avoiding floating point issues
    size_t d_fl = static_cast<size_t>(floor(d)),
           d_ce = static_cast<size_t>(ceil(d));

    // 0.1 is insignificant enough that we can round down.
    if (d - d_fl < 0.1)
        return d_fl;
    else
        return d_ce;
}

double
mem_bb_logical_error_rate(size_t d)
{
    if (d == 6)
        return 7e-5;
    else if (d == 12)
        return 2e-7;
    else if (d == 18)
        return 2e-12;
    else  // (d = 24)
        return 2e-17;  // don't actually know this one
}

size_t
mem_bb_distance_for_target_logical_error_rate(double e)
{
    if (e >= 7e-5)
        return 6;
    else if (e >= 2e-7)
        return 12;
    else if (e >= 2e-12)
        return 18;
    else  // (e = 2e-17)
        return 24;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct factory_info
{
    std::string which;
    size_t      sc_dx;
    size_t      sc_dz;
    size_t      sc_dm;
    double      e_out;
};

sim::T_FACTORY*
_create_factory(const factory_info& fi, double freq_khz, size_t level, size_t buffer_capacity=4)
{
    size_t initial_input_count;
    size_t output_count;
    size_t num_rotation_steps;

    if (fi.which == "15to1")
    {
        initial_input_count = 4;
        output_count = 1;
        num_rotation_steps = 11;
    }
    else if (fi.which == "20to4")
    {
        initial_input_count = 3;
        output_count = 4;
        num_rotation_steps = 17;
    }
    else
    {
        throw std::runtime_error("_create_factory: unknown factory type " + fi.which);
    }

    sim::T_FACTORY* f = new sim::T_FACTORY{freq_khz, 
                                            fi.e_out, 
                                            initial_input_count,
                                            output_count,
                                            num_rotation_steps,
                                            buffer_capacity,
                                            level};
    return f;
}

// returns the factory vector and the actual qubits used
std::pair<std::vector<sim::T_FACTORY*>, size_t>
fact_create_factory_config_for_target_logical_error_rate(
        double e, 
        size_t max_phys_qubits,
        uint64_t t_round_ns,
        int64_t qh_reduce_which,
        double qh_reduce_fraction,
        size_t pin_limit=4)
{
    // assuming `PHYS_ERROR == 1e-3`:

    std::vector<factory_info> factory_conf;
    factory_conf.reserve(2);

    if (e >= 1e-8)
    {
        factory_conf.push_back({"15to1", 17, 7, 7, 1e-8});
    }
    else if (e >= 1e-10)
    {
        factory_conf.push_back({"15to1", 13, 5, 5, 1e-7});
        factory_conf.push_back({"20to4", 23, 11, 13, 1e-10});
    }
    else if (e >= 1e-12)
    {
        factory_conf.push_back({"15to1", 11, 5, 5, 1e-6});
        factory_conf.push_back({"15to1", 25, 11, 11, 1e-12});
    }
    else if (e >= 1e-14)
    {
        factory_conf.push_back({"15to1", 13, 5, 5, 1e-7});
        factory_conf.push_back({"15to1", 29, 11, 13, 1e-14});
    }
    else
    {
        factory_conf.push_back({"15to1", 17, 7, 7, 1e-18});
        factory_conf.push_back({"15to1", 41, 17, 17, 1e-18});
    }

    // we will make one L2 factory for every `L2_L1_RATIO` L1 factories:
    constexpr size_t L2_L1_RATIO{8};

    bool l2_factory_exists = (factory_conf.size() > 1);

    std::vector<sim::T_FACTORY*> factories;
    size_t qubit_count{0};

    uint64_t l1_round_ns = (qh_reduce_which == 3 || qh_reduce_which == 4) 
                                ? static_cast<uint64_t>(t_round_ns * (1-qh_reduce_fraction)) 
                                : t_round_ns;
    uint64_t l2_round_ns = (qh_reduce_which == 3) 
                                ? static_cast<uint64_t>(t_round_ns * (1-qh_reduce_fraction)) 
                                : t_round_ns;

    size_t l1_fact_count{0};
    size_t l2_fact_count{0};
    while ((qubit_count < max_phys_qubits || l1_fact_count == 0 || (l2_factory_exists && l2_fact_count == 0))
            && ((!l2_factory_exists && l1_fact_count <= pin_limit) || (l2_factory_exists && l2_fact_count <= pin_limit)))
    {
        // check if there is an L2 factory in our spec. If so, make one
        if (l2_factory_exists)
        {
            double freq_khz = sim::compute_freq_khz(l2_round_ns, factory_conf[1].sc_dm);
            factories.push_back(_create_factory(factory_conf[1], freq_khz, 1));
            
            size_t sc_q_count = sc_phys_qubit_count(factory_conf[1].sc_dx, factory_conf[1].sc_dz);
            qubit_count += sc_q_count * fact_logical_qubit_count(factory_conf[1].which);
            l2_fact_count++;
        }

        for (size_t i = 0; i < L2_L1_RATIO 
                            && (qubit_count < max_phys_qubits || l1_fact_count == 0) 
                            && (l2_factory_exists || l1_fact_count <= pin_limit); i++)
        {
            double freq_khz = sim::compute_freq_khz(l1_round_ns, factory_conf[0].sc_dm);
            factories.push_back(_create_factory(factory_conf[0], freq_khz, 0));

            size_t sc_q_count = sc_phys_qubit_count(factory_conf[0].sc_dx, factory_conf[0].sc_dz);
            qubit_count += sc_q_count * fact_logical_qubit_count(factory_conf[0].which);
            l1_fact_count++;
        }
    }

    if (l2_factory_exists)
    {
        std::vector<sim::T_FACTORY*> l1_fact, l2_fact;
        std::copy_if(factories.begin(), factories.end(), std::back_inserter(l1_fact),
                    [] (auto* f) { return f->level_ == 0; });
        std::copy_if(factories.begin(), factories.end(), std::back_inserter(l2_fact),
                    [] (auto* f) { return f->level_ == 1; });

        if (l1_fact.empty())
            throw std::runtime_error("fact_create_factory_config_for_target_logical_error_rate: no L1 factories found");

        for (auto* f : l1_fact)
            f->next_level_ = l2_fact;
        for (auto* f : l2_fact)
            f->previous_level_ = l1_fact;
    }

    return {factories, qubit_count};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// want a 99% success rate for the application
const double TARGET_APP_SUCCESS_RATE{0.99};

struct ITERATION_CONFIG
{
    // target error rates are used to determine code distance and factory config -- probably the only thing that is modified between iterations
    double cmp_target_error_rate_per_cycle;
    double mem_bb_target_error_rate_per_cycle;
    double fact_target_error_rate_per_gate;

    // simulation setup:
    size_t      num_program_qubits;
    std::string trace;
    int64_t     inst_sim;
    int64_t     inst_assume_total;

    // compute setup:
    int64_t                          cmp_sc_count;
    sim::COMPUTE::REPLACEMENT_POLICY cmp_repl;
    int64_t                          cmp_sc_round_ns;

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
    int64_t qh_reduce_which;
    double  qh_reduce_fraction;

    // output of an iteration:
    double application_success_rate;
};

ITERATION_CONFIG
sim_iteration(ITERATION_CONFIG conf, size_t sim_iter)
{
    constexpr size_t MIN_QUBITS{4};

    size_t num_program_qubits = conf.num_program_qubits;
    std::string trace = conf.trace;
    int64_t     inst_sim = conf.inst_sim;
    int64_t     inst_assume_total = conf.inst_assume_total;

    int64_t                          cmp_sc_count = conf.cmp_sc_count;
    sim::COMPUTE::REPLACEMENT_POLICY cmp_repl = conf.cmp_repl;
    uint64_t                         cmp_sc_round_ns = conf.cmp_sc_round_ns;

    size_t mem_bb_num_modules = conf.mem_bb_num_modules;
    size_t mem_bb_qubits_per_bank = conf.mem_bb_qubits_per_bank;
    uint64_t mem_bb_round_ns = conf.mem_bb_round_ns;
    bool     mem_is_remote = conf.mem_is_remote;
    int64_t  mem_epr_buffer_capacity = conf.mem_epr_buffer_capacity;
    int64_t  mem_mean_epr_generation_time_ns = conf.mem_mean_epr_generation_time_ns;

    int64_t fact_phys_qubits_per_program_qubit = conf.fact_phys_qubits_per_program_qubit;

    int64_t qh_reduce_which = conf.qh_reduce_which;
    double qh_reduce_fraction = conf.qh_reduce_fraction;

    // 1. determine number of surface code qubits
    int64_t cmp_sc_adjusted_round_ns = (qh_reduce_which == 1)
                                            ? static_cast<int64_t>(cmp_sc_round_ns * (1-qh_reduce_fraction)) 
                                            : cmp_sc_round_ns;
    size_t cmp_sc_code_distance = sc_distance_for_target_logical_error_rate(conf.cmp_target_error_rate_per_cycle);
    double cmp_sc_freq_ghz = sim::compute_freq_khz(cmp_sc_adjusted_round_ns, cmp_sc_code_distance);
    size_t cmp_sc_phys_qubits = cmp_sc_count * sc_phys_qubit_count(cmp_sc_code_distance);

    size_t cmp_sc_num_patches_per_row = 4;
    size_t cmp_sc_num_rows = ceil(_fpdiv(cmp_sc_count, cmp_sc_num_patches_per_row));

    // 2. determine memory config:
    int64_t mem_bb_adjusted_round_ns = (qh_reduce_which == 2)
                                            ? static_cast<int64_t>(mem_bb_round_ns * (1-qh_reduce_fraction)) 
                                            : mem_bb_round_ns;
    size_t mem_bb_banks_per_module = (num_program_qubits > cmp_sc_count) 
                                    ? ceil(_fpdiv(num_program_qubits - cmp_sc_count, mem_bb_num_modules * mem_bb_qubits_per_bank)) 
                                    : 0;
    size_t mem_bb_code_distance = mem_bb_distance_for_target_logical_error_rate(conf.mem_bb_target_error_rate_per_cycle);
    double mem_bb_freq_ghz = sim::compute_freq_khz(mem_bb_adjusted_round_ns, mem_bb_code_distance);
    size_t mem_bb_phys_qubits = mem_bb_num_modules * mem_bb_banks_per_module * bb_phys_qubit_count(mem_bb_code_distance);
    uint64_t mem_mean_epr_generation_cycle_time = sim::convert_ns_to_cycles(mem_mean_epr_generation_time_ns, mem_bb_freq_ghz);
    
    // 2.1. create memory modules:
    std::vector<sim::MEMORY_MODULE*> mem_modules;
    for (size_t i = 0; i < mem_bb_num_modules; i++)
    {
        auto* m = new sim::MEMORY_MODULE(mem_bb_freq_ghz, mem_bb_banks_per_module, mem_bb_qubits_per_bank, 
                                        mem_is_remote, mem_epr_buffer_capacity, mem_mean_epr_generation_cycle_time);
        mem_modules.push_back(m);
    }

    // 3. determine factory config:
    size_t fact_max_phys_qubits = num_program_qubits * fact_phys_qubits_per_program_qubit;
    std::vector<sim::T_FACTORY*> t_factories;
    size_t fact_phys_qubits;
    std::tie(t_factories, fact_phys_qubits) = fact_create_factory_config_for_target_logical_error_rate(
                                                    conf.fact_target_error_rate_per_gate,
                                                    fact_max_phys_qubits,
                                                    cmp_sc_round_ns,
                                                    qh_reduce_which,
                                                    qh_reduce_fraction);
    size_t fact_l1_count = std::count_if(t_factories.begin(), t_factories.end(),
                                        [] (auto* f) { return f->level_ == 0; });
    size_t fact_l2_count = std::count_if(t_factories.begin(), t_factories.end(),
                                        [] (auto* f) { return f->level_ == 1; });

    // 4. initialize `GL_CMP`
    sim::GL_CMP = new sim::COMPUTE(cmp_sc_freq_ghz, {trace}, cmp_sc_num_rows, cmp_sc_num_patches_per_row, t_factories, mem_modules, cmp_repl);

    // 5. run simulation:
    std::cout << "------------- SIM ITERATION " << sim_iter << " -------------\n";
    sim::print_stat_line(std::cout, "CMP_CODE_DISTANCE", cmp_sc_code_distance, false);
    sim::print_stat_line(std::cout, "CMP_COMPUTE_PATCHES", cmp_sc_num_patches_per_row*cmp_sc_num_rows, false);
    sim::print_stat_line(std::cout, "FACT_L1_COUNT", fact_l1_count, false);
    sim::print_stat_line(std::cout, "FACT_L2_COUNT", fact_l2_count, false);
    sim::print_stat_line(std::cout, "MEM_BB_CODE_DISTANCE", mem_bb_code_distance, false);
    sim::print_stat_line(std::cout, "MEM_BB_BANKS_PER_MODULE", mem_bb_banks_per_module, false);
    if (mem_is_remote)
        sim::print_stat_line(std::cout, "MEM_BB_MEAN_EPR_GENERATION_CYCLE_TIME", mem_mean_epr_generation_cycle_time, false);

    // reset global variables
    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
    sim::GL_CURRENT_TIME_NS = 0;

    // 5.1. initialize each component:
    sim::GL_CMP->OP_init();
    for (auto* m : mem_modules)
        m->OP_init();
    for (auto* f : t_factories)
        f->OP_init();

    // 5.2. loop until `inst_sim` is reached
    bool done;
    do
    {
        // arbitrate events:
        sim::T_FACTORY* earliest_fact = sim::arbitrate_event_selection_from_vector(t_factories);
        sim::MEMORY_MODULE* earliest_mem = sim::arbitrate_event_selection_from_vector(mem_modules);

        bool deadlock = sim::arbitrate_event_execution(earliest_fact, earliest_mem, sim::GL_CMP);

        if (deadlock)
        {
            sim::GL_CMP->dump_deadlock_info();
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
    double cmp_error_rate_per_cycle_one_qubit = sc_logical_error_rate(cmp_sc_code_distance);
    double cmp_error_rate_per_cycle_all_qubits = cmp_error_rate_per_cycle_one_qubit * cmp_sc_count;
    double cmp_total_error = cmp_error_rate_per_cycle_all_qubits * cmp_cycles;

    // 6.2. memory error:
    double mem_cycles = static_cast<double>(mem_modules[0]->current_cycle());
    mem_cycles *= inst_sim_ratio;  // scale up `mem_cycles`
    double mem_error_rate_per_cycle_one_block = mem_bb_logical_error_rate(mem_bb_code_distance);
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

    bool jit;
    
    // compute budget can be allocated absolutely (`cmp_num_surface_codes`) or as a proportion of the total number of program qubits (`cmp_surface_code_fraction`)
    int64_t cmp_sc_count;
    int64_t cmp_repl_id;
    int64_t cmp_sc_round_ns;

    int64_t fact_phys_qubits_per_program_qubit;

    // memory budget can be allocated absolutely (`mem_num_modules`) or as a proportion of the total number of program qubits (`mem_fraction_budget`)
    int64_t mem_bb_num_modules;
    int64_t mem_bb_qubits_per_bank;
    int64_t mem_bb_round_ns;
    bool    mem_is_remote;
    int64_t mem_epr_buffer_capacity;
    double  mem_epr_generation_frequency_khz;

    int64_t qh_reduce_which;
    double  qh_reduce_fraction;

    ARGPARSE()
        .required("trace", "path to trace file", trace)
        .required("inst-sim", "number of instructions to simulate", inst_sim)
        .required("inst-assume-total", "number of instructions assumed to be in the larger program", inst_assume_total)
        // simulator verbosity:
        .optional("-p", "--print-progress", "print progress frequency", sim::GL_PRINT_PROGRESS_FREQ, -1)
        // just in time compilation (limited qubit count):
        .optional("-jit", "--just-in-time-compilation", "enable just in time compilation for limited qubit count", jit, false)
        // setup:
        .optional("-dsma", "--disable-simulator-directed-memory-access", "disable simulator directed memory access", sim::GL_DISABLE_SIMULATOR_DIRECTED_MEMORY_ACCESS, false)
        .optional("-ems", "--elide-mswap-instructions", "elide mswap instructions", sim::GL_ELIDE_MSWAP_INSTRUCTIONS, false)
        // configuration:
        .optional("", "--cmp-sc-count", "number of surface codes to allocate to compute", cmp_sc_count, 4)
        .optional("-crepl", "--cmp-repl-policy", "replacement policy for compute", cmp_repl_id, static_cast<int>(sim::COMPUTE::REPLACEMENT_POLICY::LTI))
        .optional("", "--cmp-sc-round-ns", "round time for surface code", cmp_sc_round_ns, 1200)

        .optional("", "--fact-phys-qubits-per-program-qubit", "number of physical qubits to allocate to factories", fact_phys_qubits_per_program_qubit, 50)

        .optional("", "--mem-bb-num-modules", "number of memory banks per module", mem_bb_num_modules, 2)
        .optional("", "--mem-bb-qubits-per-bank", "number of qubits per bank", mem_bb_qubits_per_bank, 12)
        .optional("", "--mem-bb-round-ns", "round time for memory banks", mem_bb_round_ns, 1800)
        .optional("", "--mem-is-remote", "enable remote memory", mem_is_remote, false)
        .optional("", "--mem-epr-buffer-capacity", "remote memory epr buffer capacity", mem_epr_buffer_capacity, 4)
        .optional("", "--mem-epr-generation-frequency", "remote memory epr generation frequency (in kHz)", mem_epr_generation_frequency_khz, 1024)

        // quantum hyperthreading parameters:
        .optional("", "--qh-reduce-which", "which component to reduce syndrome extraction time for (none=0, compute=1, memory=2, all_factory=3, l1_factory_only=4)", qh_reduce_which, 0)
        .optional("", "--qh-reduce-fraction", "fraction of syndrome extraction time to reduce", qh_reduce_fraction, 0.2)
        .parse(argc, argv);

    sim::GL_PRINT_PROGRESS = (sim::GL_PRINT_PROGRESS_FREQ > 0);
    sim::COMPUTE::REPLACEMENT_POLICY cmp_repl = static_cast<sim::COMPUTE::REPLACEMENT_POLICY>(cmp_repl_id);

    uint64_t mem_mean_epr_generation_time_ns = (1.0/mem_epr_generation_frequency_khz) * 1e6;
    
    // if `jit`, then `trace` is just the base version -- we need to create a new trace with the specified `cmp_sc_count`
    if (jit)
    {
        // Create new trace filename with instruction count
        std::string trace_dir = trace.substr(0, trace.find_last_of("/\\") + 1);
        std::string trace_filename = trace.substr(trace.find_last_of("/\\") + 1);
        
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
        MEMOPT mc(cmp_sc_count, MEMOPT::EMIT_IMPL_ID::VISZLAI, sim::GL_PRINT_PROGRESS_FREQ);
        mc.run(istrm, ostrm, inst_sim/10);
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
    conf.cmp_target_error_rate_per_cycle = 1e-12;
    conf.mem_bb_target_error_rate_per_cycle = 1e-12;
    conf.fact_target_error_rate_per_gate = 1e-12;

    conf.num_program_qubits = num_program_qubits;
    conf.trace = trace;
    conf.inst_sim = inst_sim;
    conf.inst_assume_total = inst_assume_total;

    conf.cmp_sc_count = cmp_sc_count;
    conf.cmp_repl = cmp_repl;
    conf.cmp_sc_round_ns = cmp_sc_round_ns;

    conf.mem_bb_num_modules = mem_bb_num_modules;
    conf.mem_bb_qubits_per_bank = mem_bb_qubits_per_bank;
    conf.mem_bb_round_ns = mem_bb_round_ns;
    conf.mem_is_remote = mem_is_remote;
    conf.mem_epr_buffer_capacity = mem_epr_buffer_capacity;
    conf.mem_mean_epr_generation_time_ns = mem_mean_epr_generation_time_ns;

    conf.fact_phys_qubits_per_program_qubit = fact_phys_qubits_per_program_qubit;

    size_t sim_iter{0};
    bool converged{false};
//  while (!converged)
    while (sim_iter <= 1)
    {
        conf = sim_iteration(conf, sim_iter);
        sim_iter++;
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
