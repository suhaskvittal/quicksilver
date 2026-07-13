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
    int64_t print_progress_freq;
    int64_t inst_sim,
            reaction_time,
            decoder_count,
            code_distance;
    std::string decoding_method_txt;

    ARGPARSE()
        .required("trace file", "Path to trace file", trace_file)
        .required("simulation instructions", "number of instructions to simulate", inst_sim)
        .optional("-pp", "--print-progress", "Print progress cycle frequency", print_progress_freq, 100000)
        .optional("-d", "--code-distance", "Code distance", code_distance, 23)
        .optional("-tr", "--reaction-time", "Decoder reaction time (per d cycles) in cycles", reaction_time, 10)
        .optional("-nd", "--decoder-count", "Number of decoders available", decoder_count, 128)
        .optional("", "--decoding-method", "Decoding method (\"swd\" or \"pwd\")", decoding_method_txt, "pwd")
        .parse(argc, argv);

    DecodingMethod m;
    if (decoding_method_txt == "swd")
        m = DecodingMethod::SWD;
    else if (decoding_method_txt == "pwd")
        m = DecodingMethod::PWD;
    else
        std::cerr << "unknown decoding method: " << decoding_method_txt << _die{};

    DecoderTraits traits(code_distance, reaction_time);
    
    // allocate simulation objects:
    Driver* driver = new Driver(trace_file, 
                                traits,
                                m,
                                decoder_count,
                                code_distance);
    while (driver->s_inst_done < inst_sim)
    {
        driver->tick();
        if (driver->current_cycle() % print_progress_freq == 0)
            driver->print_progress(std::cout);
    }

    print_stat_line(std::cout, "IPdC", driver->ipc() * code_distance);
    driver->t_latency.dump(std::cout);

    delete driver;
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
