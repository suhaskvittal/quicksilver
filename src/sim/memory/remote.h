/*
 *  author: Suhas Vittal
 *  date:   19 February 2026
 * */

#ifndef SIM_MEMORY_REMOTE_h
#define SIM_MEMORY_REMOTE_h

#include "sim/memory_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class REMOTE_STORAGE : public STORAGE
{
public:
    using STORAGE::access_result_type;
    using STORAGE::ACCESS_TYPE;
private:
    std::vector<PRODUCER_BASE*> top_level_epr_generators_;
public:
    REMOTE_STORAGE(double freq_khz,
                    size_t n, size_t k, size_t d,
                    size_t num_adapters,
                    cycle_type load_latency,
                    cycle_type store_latency,
                    std::vector<PRODUCER_BASE*>);
private:
    access_result_type do_memory_access(cycle_type access_latency, ACCESS_TYPE) override;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim


#endif // SIM_MEMORY_REMOTE_h
