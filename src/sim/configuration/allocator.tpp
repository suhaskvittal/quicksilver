/*
 *  author: Suhas Vittal
 *  date:   20 February 2026
 * */

#include "sim/configuration/resource_estimator.h"

namespace sim
{
namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#define TEMPL_PARAMS  template <class SPEC_TYPE, class ALLOCATOR, class QUBIT_ESTIMATOR, class BANDWIDTH_ESTIMATOR, class CONSUMPTION_ESTIMATOR>

TEMPL_PARAMS ALLOCATION
throughput_aware_allocation(size_t b, 
                            std::vector<SPEC_TYPE> specs, 
                            const ALLOCATOR& f_alloc,
                            const QUBIT_ESTIMATOR& f_est_qubit_count,
                            const BANDWIDTH_ESTIMATOR& f_est_bandwidth,
                            const CONSUMPTION_ESTIMATOR& f_est_consumption)
{
    /* 1. Identify physical qubit overheads for each production level */
    std::vector<size_t> pq_counts(specs.size());
    std::vector<double> production_rates(specs.size());
    std::vector<double> consumption_rates(specs.size());
    for (size_t i = 0; i < specs.size(); i++)
    {
        pq_counts[i] = f_est_qubit_count(s);
        // handle buffer overheads for `pq_counts[i]`
        const size_t d_buf = surface_code_distance_for_target_logical_error_rate(s.output_error_rate);
        pq_counts[i] += d_buf*(s.buffer_capacity-1);

        production_rates[i] = f_est_bandwidth(s);
        consumption_rates[i] = (i == 0) ? 0.0 : f_est_consumption(s);
    }

    const size_t pq_min_required = std::reduce(pq_counts.begin(), pq_counts.end(), size_t{0});

    /* 2. If `b < min_required_per_step`, die since we cannot allocate anything */
    if (b < pq_min_required)
    {
        std::cerr << "throughput_aware_allocation: cannot allocate any production with given budget"
                    << "\n\tbudget = " << b << ", minimum required = " << pq_min_required;
        for (size_t i = 0; < pq_counts_by_level.size(); i++)
            std::cerr << "\n\trequired for one L" << i+1 << " production = " << pq_counts_by_level[i];
        std::cerr << _die{};
    }

    /* 3. Using `production_rates` and `consumption_rates`, determine the physical qubit count required *
     *    to allocate a factory at a given level with saturated bandwidth                               */
    std::vector<size_t> pq_counts_sat(specs.size());
    std::vector<size_t> counts_for_sat_alloc(specs.size()-1);
    pq_counts_sat[0] = pq_counts[0];  // same for L1 since there is no prior level
    for (size_t i = 1; i < specs.size(); i++)
    {
        const double prod_rate_prev_level = production_rates[i-1],
                     cons_rate = consumption_rates[i];
        size_t prev_level_count = static_cast<size_t>(std::round(cons_rate / prod_rate_prev_level));
        pq_counts_sat[i] = prev_level_count*pq_counts_sat[i-1] + pq_counts[i];
        counts_for_sat_alloc[i-1] = prev_level_count;
    }

    /* 4. Greedily form allocations */

    out.producers.resize(specs.size());
    std::vector<size_t> num_allocs(specs.size(), 0);
    double curr_tp{0.0};
    size_t remaining{b};
    do
    {
        std::vector<size_t> prev_allocs(num_allocs);
        double prev_tp = curr_tp;
        for (ssize_t i = specs.size()-1; i >= 0; i--)
        {
            size_t c = pq_counts_sat[i];
            if (remaining > c)
            {
                // need to have at least one for this level
                num_allocs[i]++;
            }
            else
            {
                // we can do a bandwidth-saturating allocation multiple times
                size_t num_batch_allocs = remaining/c; 
                num_allocs[i] += num_batch_allocs;

                size_t alloc_prod{num_batch_allocs};
                for (ssize_t j = i-1; j >= 0; j--)
                {
                    alloc_prod *= counts_for_sat_alloc[j];
                    num_allocs[i] += alloc_prod;
                }

                // we can exit early since we have allocated for all levels.
                break;
            }
        }

        curr_tp = estimate_throughput_of_allocation(specs, num_allocs, f_est_bandwidth, f_est_consumption);
        if (prev_tp > curr_tp - 1e-6)
        {
            num_allocs = std::move(prev_allocs);
            break;
        }
        else
        {
            for (size_t i = 0; i < num_allocs.size(); i++)
                remaining -= pq_counts[i] * (num_allocs[i]-prev_allocs[i]);
        }
    }
    while (remaining >= pq_min_required);

    /* 5. Actually do the allocations on the heap and return */
    for (size_t i = 0; i < num_allocs.size(); i++)
        for (size_t j = 0; j < num_allocs[i]; j++)
            out.producers.push_back(f_alloc(specs[i]));
    out.physical_qubit_count = b - remaining;
    out.estimated_throughput = curr_tp;

    return out;
}

#undef TEMPL_PARAMS

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SPEC_TYPE, class BANDWIDTH_ESTIMATOR, class CONSUMPTION_ESTIMATOR> double
estimate_throughput_of_allocation(std::vector<SPEC_TYPE> specs, 
                                    const BANDWIDTH_ESTIMATOR& f_bandwidth_est,
                                    const CONSUMPTION_ESTIMATOR& f_consumption_est)
{
    return 0.0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace configuration
} // namespace sim
