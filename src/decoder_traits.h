/*
 *  author: Suhas Vittal
 *  date:   3 July 2026
 * */

#ifndef DECODER_TRAITS_h
#define DECODER_TRAITS_h

#include "globals.h"

#include <cmath>
#include <cstddef>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class DecoderTraits
{
public:
    const size_t code_distance;
    const double d_cycle_reaction_time;
public:
    /*
     * Only thing we need to initialize decoder is 
     * reaction time (`tr`) and code distance (`d`)
     * */
    constexpr DecoderTraits(size_t d, double _d_cycle_reaction_time) 
        :code_distance(d), 
        d_cycle_reaction_time(_d_cycle_reaction_time) 
    {}

    /*
     * `swd_reaction_time()` is 2x the d round reaction time since only d
     * rounds of corrections are committed for every 2d rounds.
     * */
    constexpr cycle_type swd_reaction_time() const { return iceil<cycle_type>(2*d_cycle_reaction_time); }
    
    /*
     * `pwd_reaction_time()` is 6x the `d` round reaction time as we need to
     * decode 3d rounds concurrently (assume time is `3*reaction_time`) and
     * we need to do this twice.
     * */
    constexpr cycle_type pwd_reaction_time() const { return iceil<cycle_type>(2*3*d_cycle_reaction_time); }

    /*
     * `swd_throughput()` is straightforward.
     * */ 
    constexpr double swd_throughput() const { return fpdiv(code_distance, swd_reaction_time()); }

    /*
     * `pwd_throughput()` is a bit more complicated as it depends on the number of
     * decoders provided.
     * */
    constexpr double pwd_throughput(size_t nd) const { return 2*nd*fpdiv(code_distance, pwd_reaction_time()); }
    
    /*
     * Number of decoders required to reach given amount of throughput
     * */
    constexpr size_t 
    pwd_decoders_required(double target_throughput=1.0) const
    {
        double _n = fpdiv(target_throughput*pwd_reaction_time(), 2*code_distance);
        return static_cast<size_t>( std::ceil(_n) );
    }

    constexpr bool is_swd_supported() const { return swd_throughput() < 1.0+1e-12; }  // +1e-12 for FP-error
    constexpr bool is_pwd_required() const { return !is_swd_supported(); }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif
