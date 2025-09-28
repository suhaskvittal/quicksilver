/*
    author: Suhas Vittal
    date:   24 September 2025
*/

#ifndef SIM_REMOTE_MEMORY_h
#define SIM_REMOTE_MEMORY_h

#include "sim/memory.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    Most proposals for quantum error correction assume a monolithic architecture (all components on a single substrate),
    or assume that communication is rarely an overhead. Unfortunately, photonic interconnects are so far behind that
    communication is the largest overhead.

    This file implements a remote memory module connected to compute via photonic interconnects. At a high level,
    a memory access involves the following:
        (1) Generating an EPR pair between the memory substrate and compute substrate. This can be done via
            lattice surgery:
                (a) A 2d x d logical patch is initialized between the two substrates. The middle of the patch
                    is connected via EPR pairs.
                (b) The photonic interconnect is used to perform k*d syndrome extraction rounds. k depends on
                    the success rate.
        (2) Once two EPR pairs are available, they can be consumed to execute a memory swap. One EPR
            pair is used to teleport the loaded qubit to the compute substrate, and the other is used
            to teleport the stored qubit to the memory substrate.

    For simplicity, we do not model errors and restarts in EPR generation (unlike what we do in `FACTORY`). Instead,
    we just use a mean time to generate an EPR pair.
*/

class REMOTE_MEMORY_MODULE : public MEMORY_MODULE
{
public:
    using typename MEMORY_MODULE::event_type;
    using typename MEMORY_MODULE::request_type;

    const size_t epr_buffer_capacity_;
    const uint64_t mean_epr_gen_time_ns_;
private:
    size_t epr_buffer_occu_{0};
public:
    REMOTE_MEMORY_MODULE(double freq_khz,
                            size_t num_banks,
                            size_t capacity_per_bank,
                            uint64_t mean_epr_gen_time_ns);

    void OP_init() override;
protected:
    void OP_handle_event(event_type) override;
    bool serve_memory_request(const request_type&) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif