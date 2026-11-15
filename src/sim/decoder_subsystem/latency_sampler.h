/*
 *  author: Suhas Vittal
 *  author: OpenAI Codex
 *  contributor: OpenAI Codex (switching sampler moments, 10 November 2026)
 *  contributor: OpenAI Codex (RAD matched decoder pool sizing, 10 November 2026)
 *  date:   10 November 2026
 * */

#ifndef SIM_DS_LATENCY_SAMPLER_h
#define SIM_DS_LATENCY_SAMPLER_h

#include "globals.h"

#include <algorithm>
#include <cmath>
#include <random>

namespace sim::ds
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// The constructor's mean is in cycles and std is in log space.
// mean() and std() report the continuous latency distribution in cycles.
class LognormalLatencySampler
{
    double mean_;
    double std_;
    std::mt19937_64 rng_{0};
    std::lognormal_distribution<double> distribution_;
public:
    LognormalLatencySampler(double mean, double std)
        :mean_(mean), std_(std),
        distribution_(std::log(mean) - std*std/2, std == 0 ? 1.0 : std)
    {}

    cycle_type operator()()
    {
        if (std_ == 0)
            return std::max(cycle_type{1}, static_cast<cycle_type>(std::ceil(mean_)));
        return std::max(cycle_type{1}, static_cast<cycle_type>(std::ceil(distribution_(rng_))));
    }
    double mean() const { return mean_; }
    double std() const { return mean_ * std::sqrt(std::expm1(std_*std_)); }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class S1, class S2>
class SwitchingLatencySampler
{
private:
    S1 smp1_;
    S2 smp2_;

    double switching_rate_;
    cycle_type switching_overhead_;
    std::mt19937_64 rng_{0};
    std::uniform_real_distribution<double> fpr_{0.0,1.0};
public:
    SwitchingLatencySampler(S1 x, S2 y, double switching_rate, cycle_type switching_overhead)
        :smp1_(x),
        smp2_(y), 
        switching_rate_(switching_rate),
        switching_overhead_(switching_overhead)
    {}

    cycle_type
    operator()()
    {
        cycle_type t = smp1_() + switching_overhead_;
        if (fpr_(rng_) < switching_rate_)
            t += smp2_();
        return t;
    }

    double mean() const { return smp1_.mean() + switching_overhead_ + switching_rate_*smp2_.mean(); }
    double std() const
    {
        const double p = switching_rate_;
        const double l1_std = smp1_.std(), l2_std = smp2_.std();
        const double l2_mean = smp2_.mean();
        return std::sqrt(l1_std*l1_std + p*l2_std*l2_std + p*(1-p)*l2_mean*l2_mean);
    }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class S> cycle_type
pwd_reaction_time(size_t n_dec, S& smp)
{
    cycle_type tr{0};
    cycle_type prev_latency = smp();
    for (size_t i = 1; i < n_dec; i++)
    {
        cycle_type curr_latency = smp();
        // B can start once both preceding A windows complete.
        prev_latency = std::max(prev_latency, curr_latency) + smp();
        tr = std::max(prev_latency, tr);
        prev_latency = curr_latency;
    }
    return tr;
}

template <class S> double
estimated_pwd_reaction_time(size_t n_dec, S smp)
{
    constexpr size_t TRIALS{32};
    double total{0};
    for (size_t i = 0; i < TRIALS; i++)
        total += pwd_reaction_time(n_dec, smp);
    return total / TRIALS;
}

template <class S1, class S2> size_t
matched_decoder_count(size_t l1_count, S1 l1, S2 l2)
{
    const double l1_batch_mean = estimated_pwd_reaction_time(l1_count, l1);
    const double target_throughput = l1_count / l1_batch_mean;
    size_t l2_count = std::max(size_t{2}, static_cast<size_t>(std::ceil(l1_count*l2.mean()/l1.mean())));
    for (size_t i = 0; i < 16; i++)
    {
        const double l2_batch_mean = estimated_pwd_reaction_time(l2_count, l2);
        const size_t needed = std::max(size_t{2}, static_cast<size_t>(std::ceil(target_throughput*l2_batch_mean)));
        if (needed <= l2_count)
            break;
        l2_count = needed;
    }
    return l2_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim::ds

#endif
