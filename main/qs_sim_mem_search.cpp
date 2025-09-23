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

size_t
mem_bb_distance_for_given_sc_distance(size_t d)
{
    // round d to the nearest multiple of 6
    double dby6 = static_cast<double>(d) / 6.0;
    return static_cast<size_t>(round(ceil(dby6) * 6));
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
fact_create_factory_config_for_target_logical_error_rate(double e, size_t max_phys_qubits, uint64_t t_round_ns, size_t pin_limit=4)
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

    size_t l1_fact_count{0};
    size_t l2_fact_count{0};
    while ((qubit_count < max_phys_qubits || l1_fact_count == 0 || (l2_factory_exists && l2_fact_count == 0))
            && ((!l2_factory_exists && l1_fact_count <= pin_limit) || (l2_factory_exists && l2_fact_count <= pin_limit)))
    {
        // check if there is an L2 factory in our spec. If so, make one
        if (l2_factory_exists)
        {
            double freq_khz = sim::compute_freq_khz(t_round_ns, factory_conf[1].sc_dm);
            factories.push_back(_create_factory(factory_conf[1], freq_khz, 1));
            
            size_t sc_q_count = sc_phys_qubit_count(factory_conf[1].sc_dx, factory_conf[1].sc_dz);
            qubit_count += sc_q_count * fact_logical_qubit_count(factory_conf[1].which);
            l2_fact_count++;
        }

        for (size_t i = 0; i < L2_L1_RATIO 
                            && (qubit_count < max_phys_qubits || l1_fact_count == 0) 
                            && (l2_factory_exists || l1_fact_count <= pin_limit); i++)
        {
            double freq_khz = sim::compute_freq_khz(t_round_ns, factory_conf[0].sc_dm);
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

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string trace;

    int64_t inst_sim;
    int64_t inst_sim_assume_total;
    
    // compute budget can be allocated absolutely (`cmp_num_surface_codes`) or as a proportion of the total number of program qubits (`cmp_surface_code_fraction`)
    double cmp_surface_code_fraction;
    int64_t cmp_repl_id;

    // factory budget: this is a bit complicated. We do the following to ensure a fair analysis:
    //   (1) N = number of program qubits
    //   (2) P = number of physical qubits required to implement N surface code qubits. Note that not all program qubits will be implemented
    //          using the compute budget.
    //   (3) We give magic state factories `fact_prog_fraction * P` physical qubits.
    double fact_prog_fraction;

    // memory budget can be allocated absolutely (`mem_num_modules`) or as a proportion of the total number of program qubits (`mem_fraction_budget`)
    int64_t mem_bb_num_modules;
    int64_t mem_bb_qubits_per_bank;

    // other fixed sim parameters:
    int64_t cmp_sc_round_ns;
    int64_t mem_bb_round_ns;

    // initial sim parameters:
    int64_t cmp_sc_code_distance;
    int64_t mem_bb_code_distance;

    ARGPARSE()
        .required("trace", "path to trace file", trace)
        .required("inst-sim", "number of instructions to simulate", inst_sim)
        .required("inst-sim-assume-total", "number of instructions assumed to be in the larger program", inst_sim_assume_total)
        // simulator verbosity:
        .optional("-p", "--print-progress", "print progress frequency", sim::GL_PRINT_PROGRESS_FREQ, -1)
        // setup:
        .optional("-rzpf", "--rz-prefetch", "enable rz directed prefetch", sim::GL_IMPL_RZ_PREFETCH, false)
        // configuration:
        .optional("", "--cmp-surface-code-fraction", "fraction of program qubits to allocate to surface codes", cmp_surface_code_fraction, 0.1)
        .optional("-crepl", "--cmp-repl-policy", "replacement policy for compute", cmp_repl_id, static_cast<int>(sim::COMPUTE::REPLACEMENT_POLICY::LTI))
        .optional("", "--fact-prog-fraction", "fraction of program qubits to allocate to factories", fact_prog_fraction, 0.6)
        .optional("", "--mem-bb-num-modules", "number of memory banks per module", mem_bb_num_modules, 2)
        .optional("", "--mem-bb-qubits-per-bank", "number of qubits per bank", mem_bb_qubits_per_bank, 12)
        .optional("", "--cmp-sc-round-ns", "round time for surface code", cmp_sc_round_ns, 1200)
        .optional("", "--mem-bb-round-ns", "round time for memory banks", mem_bb_round_ns, 1800)
        .optional("", "--initial-cmp-sc-code-distance", "initial surface code distance", cmp_sc_code_distance, 19)
        .parse(argc, argv);

    sim::GL_PRINT_PROGRESS = (sim::GL_PRINT_PROGRESS_FREQ > 0);

    // bb code distance is a function of the sc code distance
    sim::COMPUTE::REPLACEMENT_POLICY cmp_repl = static_cast<sim::COMPUTE::REPLACEMENT_POLICY>(cmp_repl_id);
    mem_bb_code_distance = mem_bb_distance_for_given_sc_distance(cmp_sc_code_distance);

    if (sim::GL_IMPL_RZ_PREFETCH)
        std::cout << "*** enabling rz directed prefetch ***\n";

    // read trace to identify number of program qubits:
    uint32_t num_program_qubits{};
    bool is_gz = trace.find(".gz") != std::string::npos;
    if (is_gz)
    {
        gzFile gzstrm = gzopen(trace.c_str(), "rb");
        gzread(gzstrm, &num_program_qubits, 4);
        gzclose(gzstrm);
    }
    else
    {
        FILE* strm = fopen(trace.c_str(), "rb");
        fread(&num_program_qubits, 4, 1, strm);
        fclose(strm);
    }

    // start sim loop:
    size_t sim_iter{0};
    bool converged{false};
//  while (!converged)
    while (sim_iter == 0)
    {
        constexpr size_t MIN_QUBITS{4};

        // figure out variable sim parameters:
        double target_error_rate_per_cycle = sc_logical_error_rate(cmp_sc_code_distance);

        // require a 4 qubit minimum
        size_t cmp_count = ceil(cmp_surface_code_fraction*num_program_qubits);
        cmp_count = std::max(MIN_QUBITS, cmp_count);

        size_t cmp_phys_qubits = cmp_count * sc_phys_qubit_count(cmp_sc_code_distance);
        size_t cmp_patches_per_row = MIN_QUBITS;
        size_t cmp_num_rows = ceil(_fpdiv(cmp_count, cmp_patches_per_row));
        double cmp_freq_ghz = sim::compute_freq_khz(cmp_sc_round_ns, cmp_sc_code_distance);

        size_t mem_bb_banks_per_module = (num_program_qubits > cmp_count) 
                                        ? ceil(_fpdiv(num_program_qubits - cmp_count, mem_bb_num_modules * mem_bb_qubits_per_bank)) 
                                        : 0;
        double mem_bb_freq_ghz = sim::compute_freq_khz(mem_bb_round_ns, mem_bb_code_distance);

        size_t fact_max_phys_qubits = ceil(num_program_qubits * sc_phys_qubit_count(cmp_sc_code_distance) * fact_prog_fraction);

        // initialize factories:
        std::vector<sim::T_FACTORY*> t_factories;
        size_t fact_phys_qubits;
        std::tie(t_factories, fact_phys_qubits) = fact_create_factory_config_for_target_logical_error_rate(
                                                        target_error_rate_per_cycle,
                                                        fact_max_phys_qubits,
                                                        cmp_sc_round_ns);
        size_t fact_l1_count = std::count_if(t_factories.begin(), t_factories.end(),
                                            [] (auto* f) { return f->level_ == 0; });
        size_t fact_l2_count = std::count_if(t_factories.begin(), t_factories.end(),
                                            [] (auto* f) { return f->level_ == 1; });

        // initialize memory:
        std::vector<sim::MEMORY_MODULE*> mem_modules{};
        for (size_t i = 0; i < mem_bb_num_modules; i++)
        {
            auto* m = new sim::MEMORY_MODULE(mem_bb_freq_ghz, mem_bb_banks_per_module, mem_bb_qubits_per_bank);
            mem_modules.push_back(m);
        }
        size_t mem_bb_phys_qubits = mem_bb_num_modules * mem_bb_banks_per_module * bb_phys_qubit_count(mem_bb_code_distance);

        // initialize compute
        sim::GL_CMP = new sim::COMPUTE(cmp_freq_ghz, {trace}, cmp_num_rows, cmp_patches_per_row, t_factories, mem_modules, cmp_repl);

        // run simulation until EOF
        std::cout << "------------- SIM ITERATION " << sim_iter << " -------------\n";
        sim::print_stat_line(std::cout, "CMP_CODE_DISTANCE", cmp_sc_code_distance, false);
        sim::print_stat_line(std::cout, "CMP_COMPUTE_PATCHES", cmp_patches_per_row*cmp_num_rows, false);
        sim::print_stat_line(std::cout, "FACT_L1_COUNT", fact_l1_count, false);
        sim::print_stat_line(std::cout, "FACT_L2_COUNT", fact_l2_count, false);
        sim::print_stat_line(std::cout, "MEM_BB_CODE_DISTANCE", mem_bb_code_distance, false);
        sim::print_stat_line(std::cout, "MEM_BB_BANKS_PER_MODULE", mem_bb_banks_per_module, false);

        // run initialization:
        sim::GL_CMP->OP_init();
        for (auto* m : mem_modules)
            m->OP_init();
        for (auto* f : t_factories)
            f->OP_init();


        sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();
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

        // now that the simulation is done -- analyze the stats to determine if we have converged or not
        // for simplicity, assume that the bottleneck is the amount of compute cycles -- this is the case for
        // our setup, since we have set up the magic state factories to spit out magic states at a rate lower
        // than the logical error rate
        
        // compute required error rate that would have achieved the application success rate anyways.
        double cmp_cycles = static_cast<double>(sim::GL_CMP->current_cycle());

        // scale cycles by `inst_sim_assume_total/inst_sim`
        double inst_sim_ratio = static_cast<double>(inst_sim_assume_total) / static_cast<double>(inst_sim);
        cmp_cycles *= inst_sim_ratio;

        double log_1_minus_acceptable_error_rate_per_cycle = (1.0/cmp_cycles) * log(TARGET_APP_SUCCESS_RATE);
        double acceptable_error_rate_per_cycle = 1.0 - exp(log_1_minus_acceptable_error_rate_per_cycle);
        size_t required_code_distance = sc_distance_for_target_logical_error_rate(acceptable_error_rate_per_cycle);

        sim::print_stats(std::cout);
        sim::print_stat_line(std::cout, "INST_SIM_RATIO", inst_sim_ratio, false);
        sim::print_stat_line(std::cout, "SCALED_CMP_CYCLES", cmp_cycles, false);
        sim::print_stat_line(std::cout, "L1_FACTORY_COUNT", fact_l1_count, false);
        sim::print_stat_line(std::cout, "L2_FACTORY_COUNT", fact_l2_count, false);
        sim::print_stat_line(std::cout, "COMPUTE_TOTAL_PHYSICAL_QUBITS", cmp_phys_qubits, false);
        sim::print_stat_line(std::cout, "FACTORY_TOTAL_PHYSICAL_QUBITS", fact_phys_qubits, false);
        sim::print_stat_line(std::cout, "MEMORY_TOTAL_PHYSICAL_QUBITS", mem_bb_phys_qubits, false);

        sim::print_stat_line(std::cout, "SIMULATED_CODE_DISTANCE", cmp_sc_code_distance, false);
        sim::print_stat_line(std::cout, "SIMULATED_ERROR_RATE_PER_CYCLE", target_error_rate_per_cycle, false);

        sim::print_stat_line(std::cout, "REQUIRED_CODE_DISTANCE", required_code_distance, false);
        sim::print_stat_line(std::cout, "REQUIRED_ERROR_RATE_PER_CYCLE", acceptable_error_rate_per_cycle, false);

        bool within_distance = (cmp_sc_code_distance <= required_code_distance+1 && cmp_sc_code_distance >= required_code_distance-1);
        if (within_distance || (sim_iter >= 3 && required_code_distance <= cmp_sc_code_distance))
        {
            converged = true;
        }
        else 
        {
            // need to adjust code distances:
            cmp_sc_code_distance = required_code_distance;
            mem_bb_code_distance = mem_bb_distance_for_given_sc_distance(cmp_sc_code_distance);
        }

        // deallocate memory:
        delete sim::GL_CMP;
        for (auto* f : t_factories)
            delete f;
        for (auto* m : mem_modules)
            delete m;

        sim_iter++;
    }

    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////