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

#include "sim.h"