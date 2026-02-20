/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#ifndef PRODUCTION_EPR_h
#define PRODUCTION_EPR_h

#include "sim/production.h"

namespace sim
{
namespace producer
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class ENT_DISTILLATION : public PRODUCER_BASE
{
public:
    const size_t input_count;
    const size_t output_count;
    const size_t num_checks;
private:
    size_t step_{0};
    size_t inputs_available_{0};

    /*
     * We will only know at the end of the protocol whether
     * we will discard (since this is when the syndromes
     * are communicated between Alice and Bob).
     *
     * So track probability of error and call RNG at the end.
     * */
    double error_probability_{0.0};

    /*
     * We assume that if an input EPR pair is retrieved from
     * a lower-level protocol, there is a 1 cycle "readout"
     * overhead (need to project logical state onto surface
     * code qubit).
     * */
    bool awaiting_input_{false};
public:
    ENT_DISTILLATION(double freq_khz,
                        double output_error_prob,
                        size_t buffer_capacity,
                        size_t input_count,
                        size_t output_count,
                        size_t num_checks);
private:
    bool production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace producer
} // namespace sim

#endif // PRODUCTION_EPR_h
