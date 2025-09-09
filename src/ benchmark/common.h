/*
    author: Suhas Vittal
    date:   20 September 2025
*/

#ifndef BENCHMARK_COMMON_h
#define BENCHMARK_COMMON_h

#include "instruction.h"

#include <utility>

namespace benchmark
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using qubit_register_range_type = std::pair<qubit_type, qubit_type>;

void qft(gzFile, qubit_register_range_type, bool inverse=false);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace benchmark

#endif  // BENCHMARK_COMMON_h