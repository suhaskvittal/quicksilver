/*
 *  author: Suhas Vittal
 *  date:   20 February 2026
 * */

#include "sim/configuration/resource_estimation.h"

#include <cassert>

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
        const auto& s = specs[i];
        pq_counts[i] = f_est_qubit_count(s);
        production_rates[i] = f_est_bandwidth(s);
        consumption_rates[i] = (i == 0) ? 0.0 : f_est_consumption(s);
    }

    const size_t pq_min_required = std::reduce(pq_counts.begin(), pq_counts.end(), size_t{0});

    /* 2. If `b < min_required_per_step`, die since we cannot allocate anything */
    if (b < pq_min_required)
    {
        std::cerr << "throughput_aware_allocation: cannot allocate any production with given budget"
                    << "\n\tbudget = " << b << ", minimum required = " << pq_min_required;
        for (size_t i = 0; i < pq_counts.size(); i++)
            std::cerr << "\n\trequired for one L" << i+1 << " production = " << pq_counts[i];
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

    std::vector<size_t> counts(specs.size(), 0);
    double curr_tp{0.0};
    size_t remaining{b};
    do
    {
        std::vector<size_t> prev_counts(counts);
        double prev_tp{curr_tp};
        size_t remaining_in_loop{remaining};
        for (ssize_t i = specs.size()-1; i >= 0; i--)
        {
            size_t c = pq_counts_sat[i];
            if (remaining_in_loop < c)
            {
                // need to have at least one for this level
                counts[i]++;
                remaining_in_loop -= pq_counts[i];
            }
            else
            {
                // we can do a bandwidth-saturating allocation multiple times
                size_t num_batch_allocs = remaining_in_loop/c; 
                counts[i] += num_batch_allocs;

                // now note that `counts_for_sat_alloc` only contains the count for
                // that given level to saturate the next level. So the total count
                // for a given level is obtained by taking a product.
                //
                // i.e.,
                //  counts_for_sat_alloc = [ 8, 4, * ]   (note that the last entry is never set)
                //  products =             [ 32, 4, 1 ]   if `num_batch_allocs = 1`
                size_t alloc_prod{num_batch_allocs};
                for (ssize_t j = i-1; j >= 0; j--)
                {
                    alloc_prod *= counts_for_sat_alloc[j];
                    counts[j] += alloc_prod;
                }

                // we can exit early since we have allocated for all levels.
                break;
            }
        }

        curr_tp = estimate_throughput_of_allocation(specs, counts, f_est_bandwidth, f_est_consumption);
        if (prev_tp > curr_tp - 1e-6)
        {
            counts = std::move(prev_counts);
            break;
        }
        else
        {
            size_t qubit_count{0};
            for (size_t i = 0; i < counts.size(); i++)
                qubit_count += pq_counts[i] * (counts[i]-prev_counts[i]);
            remaining -= qubit_count;
        }
    }
    while (remaining >= pq_min_required);

    /* 5. Actually do the allocations on the heap and return */
    ALLOCATION out{};
    out.producers.resize(specs.size());
    for (size_t i = 0; i < counts.size(); i++)
    {
        for (size_t j = 0; j < counts[i]; j++)
        {
            auto* p = f_alloc(specs[i]);
            if (i > 0)
                p->previous_level = out.producers[i-1];
            out.producers[i].push_back(p);
        }
    }
    out.physical_qubit_count = b - remaining;
    out.estimated_throughput = curr_tp;

    return out;
}

#undef TEMPL_PARAMS

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class SPEC_TYPE, class BANDWIDTH_ESTIMATOR, class CONSUMPTION_ESTIMATOR> double
estimate_throughput_of_allocation(const std::vector<SPEC_TYPE>& specs,
                                    const std::vector<size_t>& counts,
                                    const BANDWIDTH_ESTIMATOR& f_bandwidth_est,
                                    const CONSUMPTION_ESTIMATOR& f_consumption_est)
{
    double prod_rate = counts[0] * f_bandwidth_est(specs[0]);
    for (size_t i = 1; i < specs.size(); i++)
    {
        double cons_rate = counts[1] * f_consumption_est(specs[i]);
        // estimate the ratio between the consumption and production rates
        //
        //  if `prod_rate > cons_rate`, then the new production rate for this level is
        //  just the production rate (unmodified) for this given level
        //
        //  else, then the production rate is scaled by `prod_rate / cons_rate` since
        //  lower level bandwidth is not maximized
        double r = std::min(1.0, prod_rate / cons_rate);
        prod_rate = r * counts[i] * f_bandwidth_est(specs[1]);
    }
    return prod_rate;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace configuration
} // namespace sim
