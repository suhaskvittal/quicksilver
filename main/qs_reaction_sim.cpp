/*
 *  author: Suhas Vittal
 *  date:   6 July 2026
 * */

#include "qs_reaction_sim/sim.h"

#include <argparse/argparse.h>

#include <iostream>

using namespace rs;

namespace
{







} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int 
main(int argc, char* argv[])
{
    std::string trace_file;
    int64_t inst_sim,
            reaction_time,
            decoder_count,
            code_distance;
    std::string decoding_method_txt;

    ARGPARSE()
        .required("trace file", "Path to trace file", trace_file)
        .required("simulation instructions", "number of instructions to simulate", inst_sim)
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
    cycle_type last_cycle_no_progress{0};
    while (driver->s_inst_done < inst_sim)
    {
        if (driver->current_cycle() - last_cycle_no_progress > 100'000)
            std::cerr << "deadlock detected" << _die{};
        long progress = driver->operate();
        if (progress > 0)
            last_cycle_no_progress = driver->current_cycle();
    }

    print_stat_line(std::cout, "IPdC", fpdiv(driver->ipc(), code_distance));
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
