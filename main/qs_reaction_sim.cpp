/*
 *  author: Suhas Vittal
 *  date:   6 July 2026
 * */

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

    // argparse only supports int64_t/double/std::string/bool, so integer options
    // are parsed into locals and copied into `conf` (whose fields are size_t/cycle_type).
    int64_t code_distance,
            reaction_time,
            decoder_count,
            rad_reaction_time,
            rad_decoder_count;
    double  rad_error_rate;

    ARGPARSE()
        .required("trace file", "Path to trace file", trace_file)
        .required("simulation instructions", "number of instructions to simulate", inst_sim)
        .optional("-pp", "--print-progress", "Print progress cycle frequency", print_progress_freq, 100000)
        .optional("-d", "--code-distance", "Code distance", code_distance, 23)
        .optional("-tr", "--reaction-time", "Decoder reaction time (per d cycles) in cycles", reaction_time, 10)
        .optional("-nd", "--decoder-count", "Number of decoders available", decoder_count, 128)

        /*
         * RAD arguments:
         * */
        .optional("-rad", "--rad", "Enable RAD", GL_RAD_ENABLED, false)
        .optional("", "--rad-reaction-time", "RAD slow decoder reaction time", rad_reaction_time, 100)
        .optional("", "--rad-decoder-count", "RAD slow decoder count", rad_decoder_count, 1024)
        .optional("", "--rad-error-rate", "RAD fast-decoder per-window error probability", rad_error_rate, 0.0)

        .parse(argc, argv);

    Driver::config_type conf;
    conf.code_distance = code_distance;
    conf.reaction_time = reaction_time;
    conf.decoder_count = decoder_count;
    conf.rad.reaction_time = rad_reaction_time;
    conf.rad.decoder_count = rad_decoder_count;
    conf.rad.fast_decoder_error_probability = rad_error_rate;

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
    driver->s_t_latency.dump(std::cout);
    driver->s_cx_routing_overhead.dump(std::cout);
    driver->s_t_routing_overhead.dump(std::cout);

    delete driver;
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
