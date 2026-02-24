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
    /*
     * Inner code measurement distance. Determines the number of
     * rounds per check measurement.
     * */
    const size_t measurement_distance;
    const size_t num_checks;
private:
    size_t step_{0};
    size_t inputs_available_{0};

    /*
     * The simulation cycle at which the current PPM measurement completes.
     * Only meaningful when step_ > 0.
     * */
    cycle_type cycle_available_{0};

    /*
     * We will only know at the end of the protocol whether
     * we will discard (since this is when the syndromes
     * are communicated between Alice and Bob).
     *
     * So track probability of error and call RNG at the end.
     * */
    double error_probability_{0.0};
public:
    ENT_DISTILLATION(double freq_khz,
                        double output_error_prob,
                        size_t buffer_capacity,
                        size_t input_count,
                        size_t output_count,
                        size_t measurement_distance,
                        size_t num_checks);

    /*
     * Returns the next expected cycle with progress. This is
     * an "expectation" as distillation is non-deterministic.
     *
     * This is primarily used to compute a skip cycle.
     * */
    cycle_type get_next_progression_cycle() const;
private:
    bool production_step() override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace producer
} // namespace sim

#endif // PRODUCTION_EPR_h
