/*
 *  author: Suhas Vittal
 *  date:   18 February 2026
 * */

#ifndef SIM_PRODUCTION_h
#define SIM_PRODUCTION_h

#include "globals.h"
#include "sim/operable.h"

#include <deque>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class PRODUCER_BASE : public OPERABLE
{
public:
    const double output_error_probability;
    const size_t buffer_capacity;

    /*
     * Producers that produce resource states for consumption
     * by this producer. If empty, then it should be assumed
     * that this is a first-level production (state-injection).
     * */
    std::vector<PRODUCER_BASE*> previous_level;

    /*
     * Statistics:
     * */
    uint64_t s_production_attempts{0};
    uint64_t s_failures{0};
    uint64_t s_consumed{0};
protected:
    /*
     * Number of resources in local buffer (max `buffer_capacity`)
     * */
    size_t buffer_occupancy_{0};
public:
    PRODUCER_BASE(std::string_view name, 
                    double freq_khz, 
                    double output_error_prob,
                    size_t buffer_capacity);

    /*
     * Safely consumes `count` resource states from the buffer.
     * */
    void consume(size_t count);

    void print_deadlock_info(std::ostream&) const override;

    size_t buffer_occupancy() const;
protected:
    long operate() override;

    /*
     * Adds a resource state to the buffer. Marked virtual
     * in case other classes want to add any stats atop
     * this function.
     * */
    virtual void install_resource_state();

    /*
     * `operate()` calls `production_step`, which advances
     * magic state production by one cycle. Implementation
     * is defined by descendant class.
     *
     * This function should return true if anything was
     * attempted (regardless of failure).
     * */
    virtual bool production_step() =0;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim

#endif  // SIM_PRODUCTION_h
