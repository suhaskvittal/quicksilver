/*
 *  author: Suhas Vittal
 *  date:   10 November 2026
 *  contributor: OpenAI Codex (decoder event interval API, 10 November 2026)
 *  contributor: OpenAI Codex (latency sampler extraction, 10 November 2026)
 *  contributor: OpenAI Codex (RAD reaction time selection, 10 November 2026)
 * */

#ifndef SIM_DECODER_SUBSYSTEM_h
#define SIM_DECODER_SUBSYSTEM_h

#include "sim/operable.h"
#include "sim/decoder_subsystem/latency_sampler.h"
#include "instruction.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace sim
{

class Qubit;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

struct DecoderSubsystemBase : public Operable
{
    using inst_ptr = Instruction*;

    explicit DecoderSubsystemBase(double freq_khz)
        :Operable("decoder_subsystem", freq_khz)
    {}
    virtual ~DecoderSubsystemBase() =default;

    // The event occupies [start_cycle, end_cycle); stv includes work in this interval.
    virtual void process_event(cycle_type start_cycle, cycle_type end_cycle,
                               size_t stv, std::vector<Qubit*>, inst_ptr) {}
    virtual cycle_type reaction_time() =0;
    virtual cycle_type reaction_time_for(Qubit*) { return reaction_time(); }

    long operate() override { return 1; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * `LatencySampler` should expose the following:
 *  -- `LatencySampler::operator()` should sample from the underlying distribution
 *  -- `LatencySampler::mean()` and `LatencySampler::std()` should return mean and std as a double.
 * */
template <class LatencySampler>
class PhenoDecoderSubsystem : public DecoderSubsystemBase
{
public:
    const size_t code_distance;
    const size_t decoder_count;
private:
    LatencySampler sampler_;
public:
    PhenoDecoderSubsystem(double freq_khz, size_t d, LatencySampler s,
                          std::optional<size_t> decoder_count_override = std::nullopt)
        :DecoderSubsystemBase(freq_khz),
        code_distance(d),
        decoder_count(decoder_count_override ? std::max(size_t{2}, *decoder_count_override)
                                             : number_of_decoders(s)),
        sampler_(s)
    {}

    cycle_type reaction_time() override { return ds::pwd_reaction_time(decoder_count, sampler_); }
    double reaction_time_mean() const { return sampler_.mean(); }
    double reaction_time_std() const { return sampler_.std(); }
private:
    size_t
    number_of_decoders(LatencySampler s) const
    {
        // pre-condition: `LatencySampler::mean()` returns latency for 3d rounds 
        size_t n = static_cast<size_t>( std::ceil(1.1 * fpdiv(s.mean(), code_distance)) ); 
        return std::max(size_t{2}, n);
    }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif
