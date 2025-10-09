/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#ifndef SIM_UTILS_ENUMS_h
#define SIM_UTILS_ENUMS_h

namespace sim
{

enum class QHT_LATENCY_REDUCTION_TARGET
{
    NONE,
    COMPUTE,
    MEMORY,
    ALL_FACTORY,
    L1_FACTORY_ONLY
};

}   // namespace sim

#endif  // SIM_UTILS_ENUMS_h
