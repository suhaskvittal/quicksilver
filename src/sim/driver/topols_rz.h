#ifndef SIM_DRIVER_TOPOLS_RZ_h
#define SIM_DRIVER_TOPOLS_RZ_h

#include "instruction.h"
#include "sim/compute_subsystem.h"

#include <vector>

namespace sim::driver
{

/*
 * Timing/resource approximation of the external-ancilla Rz construction in
 * TopoLS Appendix A. The ZZ measurement precedes ancilla-side preparation;
 * dependent data instructions wait for preparation and X readout. The simulator
 * has no Pauli-frame state, so it does not model speculative data continuation.
 */
class TopolsRz
{
    struct Request
    {
        Instruction* inst;
        Qubit* data;
        Qubit* ancilla;
        cycle_type readout_ready{0};
        bool readout_started{false};
    };

    ComputeSubsystem* compute_;
    std::vector<Qubit*> free_ancilla_;
    std::vector<Request> active_;
public:
    explicit TopolsRz(ComputeSubsystem*);

    bool start(Instruction*, Qubit*);
    bool contains(Instruction*) const;
    bool is_done(Instruction*) const;
    void retire(Instruction*);
    long operate();
};

} // namespace sim::driver

#endif
