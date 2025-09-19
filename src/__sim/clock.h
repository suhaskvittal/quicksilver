/*
    author: Suhas Vittal
    date:   27 August 2025
*/

#ifndef SIM_CLOCK_h
#define SIM_CLOCK_h

#include <cstdint>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class CLOCKABLE
{
public:
    const double freq_khz_;
protected:
    uint64_t cycle_{0};
private:
    double leap_{0.0};
    double clk_scale_;
public:
    CLOCKABLE(double freq_khz);

    void tick();
    
    // descendants should implement this, as it is called by `tick`
    virtual void operate() =0;

    double clock_scale() const { return clk_scale_; }
private:
    friend void setup_clk_scale_for_group(std::vector<CLOCKABLE*>);
};

// sets up the clock scale for a group of clockables, based on the maximum frequency across
// all clockables in the group (this is the reference frequency)
void setup_clk_scale_for_group(std::vector<CLOCKABLE*>);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double compute_freq_khz(uint64_t t_sext_round_ns, size_t num_rounds_per_cycle);
uint64_t convert_cycles_between_frequencies(uint64_t t_cycles, double freq_khz_from, double freq_khz_to);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_CLOCK_h