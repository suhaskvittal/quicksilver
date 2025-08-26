/*
    author: Suhas Vittal
    date:   26 August 2025
*/

#include "sim.h"

int 
main(int argc, char** argv)
{
    // parse input arguments;

    sim::CONFIG cfg;

    sim::SIM sim(cfg);

    while (!sim.is_done())
        sim.tick();

    // print stats for each client:

    return 0;
}