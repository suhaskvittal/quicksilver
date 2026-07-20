/*
 *  author: Suhas Vittal
 *  date:   6 July 2026
 * */

#include "qs_reaction_sim/rad.h"
#include "qs_reaction_sim/sim.h"

#include <argparse/argparse.h>

#include <iostream>

using namespace rs;

namespace sim
{


int64_t GL_MAX_CYCLES_WITH_NO_PROGRESS = 10000;


} // namespace sim

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int 
main(int argc, char* argv[])
{
    std::string trace_file;
    int64_t inst_sim,
            print_progress_freq;

    Driver::config_type conf;

    ARGPARSE()
        .required("trace file", "Path to trace file", trace_file)
        .required("simulation instructions", "number of instructions to simulate", inst_sim)
        .optional("-pp", "--print-progress", "Print progress cycle frequency", print_progress_freq, 100000)
        .optional("-d", "--code-distance", "Code distance", conf.code_distance, 23)
        .optional("-tr", "--reaction-time", "Decoder reaction time (per d cycles) in cycles", conf.reaction_time, 10)
        .optional("-nd", "--decoder-count", "Number of decoders available", conf.decoder_count, 128)

        /*
         * RAD arguments:
         * */
        .optional("-rad", "--rad", "Enable RAD", GL_RAD_ENABLED, false)
        .optional("", "--rad-reaction-time", "RAD slow decoder reaction time", conf.rad.reaction_time, 100)
        .optional("", "--rad-decoder-count", "RAD slow decoder count", conf.rad.decoder_count, 1024)
        .optional("", "--rad-error-rate", "RAD fast-decoder per-window error probability", conf.rad.fast_decoder_error_probability, 0.0)

        .parse(argc, argv);

    // allocate simulation objects:
    Driver* driver = new Driver(trace_file, conf);
    while (driver->s_inst_done < inst_sim)
    {
        driver->tick();
        if (driver->current_cycle() % print_progress_freq == 0)
            driver->print_progress(std::cout);
    }

    std::cout << "\n\nFINAL_STATS----------------------------------------\n";

    print_stat_line(std::cout, "IPdC", driver->ipc() * conf.code_distance);
    print_stat_line(std::cout, "SIM_INST", driver->s_inst_done);
    print_stat_line(std::cout, "SIM_CYCLES", driver->current_cycle());
    print_stat_line(std::cout, "T_GATES_EXECUTED", driver->s_t_gates_done);

    if (GL_RAD_ENABLED)
    {
        print_stat_line(std::cout, "T_GATES_FROM_PROGRAM_EXECUTED", driver->s_t_gates_in_program_done);
        print_stat_line(std::cout, "FAST_DECODER_THROUGHPUT", driver->decoder_traits.pwd_throughput(driver->decoder_count));
        print_stat_line(std::cout, "SLOW_DECODER_THROUGHPUT", driver->rad()->slow_decoder_traits.pwd_throughput(driver->rad()->decoder_count));
    }
    else
    {
        print_stat_line(std::cout, "DECODER_THROUGHPUT", driver->decoder_traits.pwd_throughput(driver->decoder_count));
    }

    driver->s_t_latency.dump(std::cout);
    driver->s_cx_routing_overhead.dump(std::cout);
    driver->s_t_routing_overhead.dump(std::cout);

    if (GL_RAD_ENABLED)
    {
        print_stat_line(std::cout, "WRONG_PATHS_DURING_EXECUTION", driver->rad()->s_wrong_paths);
        driver->rad()->s_wrong_path_latency.dump(std::cout);
        driver->rad()->s_wrong_path_inst_count.dump(std::cout);
        driver->rad()->s_qubits_blocked_by_wrong_path.dump(std::cout);
    }

    delete driver;
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
