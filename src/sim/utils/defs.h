/*
 *  author: Suhas Vittal
 *  date:   8 October 2025
 * */

#ifndef SIM_UTILS_ENUMS_h
#define SIM_UTILS_ENUMS_h

namespace sim
{

constexpr int64_t QHT_LATENCY_REDUCTION_TARGET_NONE{0};
constexpr int64_t QHT_LATENCY_REDUCTION_TARGET_COMPUTE{1};
constexpr int64_t QHT_LATENCY_REDUCTION_TARGET_MEMORY{2};
constexpr int64_t QHT_LATENCY_REDUCTION_TARGET_ALL_FACTORY{4};

}   // namespace sim

#endif  // SIM_UTILS_ENUMS_h
