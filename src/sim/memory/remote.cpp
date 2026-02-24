/*
 *  author: Suhas Vittal
 *  date:   19 February 2026
 * */

#include "sim/memory/remote.h"
#include "sim/production.h"
#include "sim/production/epr.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using access_type = REMOTE_STORAGE::ACCESS_TYPE;

/*
 * Returns number of distilled EPR pairs required for the given access type
 * */
constexpr size_t _get_required_epr_pairs_for_access(access_type);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

REMOTE_STORAGE::REMOTE_STORAGE(double freq_khz,
                                size_t n,
                                size_t k,
                                size_t d,
                                size_t num_adapters,
                                cycle_type load_latency,
                                cycle_type store_latency,
                                std::vector<PRODUCER_BASE*> dist)
    :STORAGE(freq_khz, n, k, d, num_adapters, load_latency, store_latency),
    top_level_epr_generators_(dist)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

REMOTE_STORAGE::access_result_type
REMOTE_STORAGE::do_memory_access(cycle_type access_latency, ACCESS_TYPE type)
{
    const size_t epr_required = _get_required_epr_pairs_for_access(type);
    const size_t epr_available = std::transform_reduce(top_level_epr_generators_.begin(),
                                                        top_level_epr_generators_.end(),
                                                        size_t{0},
                                                        std::plus<size_t>{},
                                                        [] (const auto* p) { return p->buffer_occupancy(); });
    if (epr_available < epr_required)
        return access_result_type{};

    size_t epr_remaining{epr_required};
    for (auto* p : top_level_epr_generators_)
    {
        size_t c = std::min(p->buffer_occupancy(), epr_remaining);
        p->consume(c);
        epr_remaining -= c;
        if (epr_available == 0)
            break;
    }

    return STORAGE::do_memory_access(access_latency, type);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

constexpr size_t
_get_required_epr_pairs_for_access(access_type t)
{
    switch (t)
    {
    case access_type::LOAD:
    case access_type::STORE:
        return 1;

    case access_type::COUPLED_LOAD_STORE:
        return 2;
    }

    return std::numeric_limits<size_t>::max();
}

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
