/*
 *  author: Claude (Opus 5)
 *  date:   7 September 2026
 * */

#include "sim/configuration/allocator.h"
#include "sim/configuration/resource_estimation.h"

#include <cassert>
#include <cmath>
#include <iostream>
#include <numeric>

namespace sim
{

extern double GL_PHYSICAL_ERROR_RATE;

namespace configuration
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

/*
 * Helper estimators, driven directly off `ProducerQuantum`. These replace the old
 * `QubitEstimator`/`BandwidthEstimator`/`ConsumptionEstimator` template callbacks now that
 * the quantum already carries per-unit resource-estimation metadata.
 * */

size_t
output_count(const ProducerQuantum& q)
{
    return q.is_distiller ? q.d_output_states : 1;
}

/*
 * Bare factory footprint plus the buffer overhead required to hold `q.buffer_size` output
 * states at `q.infidelity`.
 * */
size_t
physical_qubit_count_with_buffer(const ProducerQuantum& q)
{
    size_t d_buffer = surface_code_distance_for_target_logical_error_rate(q.infidelity, GL_PHYSICAL_ERROR_RATE);
    return q.physical_qubit_count + (q.buffer_size - output_count(q)) * surface_code_physical_qubit_count(d_buffer);
}

double
bandwidth_hz(const ProducerQuantum& q, double freq_khz)
{
    return (1e3 * freq_khz * output_count(q)) / q.cycles_per_production;
}

double
consumption_hz(const ProducerQuantum& q, double freq_khz)
{
    if (!q.is_distiller)
        return 0.0;
    return (1e3 * freq_khz * q.d_input_states) / q.cycles_per_production;
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

Allocation
throughput_aware_allocation(size_t b, std::vector<ProducerQuantum> quanta, double freq_khz)
{
    constexpr bool verbose{true};

    if (verbose)
        std::cout << "throughput_aware_allocation ---------------------------------------------\n";

    /* 1. Identify physical qubit overheads for each production level */
    std::vector<size_t> pq_counts(quanta.size());
    std::vector<double> production_rates(quanta.size());
    std::vector<double> consumption_rates(quanta.size());
    for (size_t i = 0; i < quanta.size(); i++)
    {
        const auto& q = quanta[i];

        pq_counts[i] = physical_qubit_count_with_buffer(q);
        production_rates[i] = bandwidth_hz(q, freq_khz);
        consumption_rates[i] = (i == 0) ? 0.0 : consumption_hz(q, freq_khz);

        if (verbose)
        {
            std::cout << "L" << i+1 << ": production rate = " << production_rates[i]
                    << ", consumption rate = " << consumption_rates[i]
                    << "\n";
        }
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
    std::vector<size_t> pq_counts_sat(quanta.size());
    std::vector<size_t> counts_for_sat_alloc(quanta.size()-1);
    pq_counts_sat[0] = pq_counts[0];  // same for L1 since there is no prior level
    for (size_t i = 1; i < quanta.size(); i++)
    {
        const double prod_rate_prev_level = production_rates[i-1],
                     cons_rate = consumption_rates[i];
        size_t prev_level_count = static_cast<size_t>(std::ceil(cons_rate / prod_rate_prev_level));
        pq_counts_sat[i] = prev_level_count*pq_counts_sat[i-1] + pq_counts[i];
        counts_for_sat_alloc[i-1] = prev_level_count;
    }

    /* 4. Greedily form allocations */

    std::vector<size_t> counts(quanta.size(), 0);
    double curr_tp{0.0};
    size_t remaining{b};
    do
    {
        std::vector<size_t> prev_counts(counts);
        double prev_tp{curr_tp};
        size_t remaining_in_loop{remaining};
        for (ssize_t i = quanta.size()-1; i >= 0; i--)
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

        curr_tp = estimate_throughput_of_allocation(quanta, counts, freq_khz);

        if (verbose)
        {
            std::cout << "prev_tp = " << prev_tp << ", curr_tp = " << curr_tp << ", delta =";
            for (size_t i = 0; i < counts.size(); i++)
                std::cout << " " << (counts[i]-prev_counts[i]);
            std::cout << "\n";
        }

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

        if (verbose)
            std::cout << "\tremaining: " << remaining << " of " << b << "\n";
    }
    while (remaining >= pq_min_required);

    /* 5. Package the per-level (quantum, count) pairs -- the caller feeds these directly to
     *    `BulkProducer::add_level`.                                                        */
    Allocation out{};
    out.levels.reserve(quanta.size());
    for (size_t i = 0; i < counts.size(); i++)
        out.levels.push_back({quanta[i], counts[i]});
    out.physical_qubit_count = b - remaining;
    out.estimated_throughput = curr_tp;

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

double
estimate_throughput_of_allocation(const std::vector<ProducerQuantum>& quanta,
                                    const std::vector<size_t>& counts,
                                    double freq_khz)
{
    double prod_rate = counts[0] * bandwidth_hz(quanta[0], freq_khz);
    for (size_t i = 1; i < quanta.size(); i++)
    {
        double cons_rate = counts[i] * consumption_hz(quanta[i], freq_khz);
        // estimate the ratio between the consumption and production rates
        //
        //  if `prod_rate > cons_rate`, then the new production rate for this level is
        //  just the production rate (unmodified) for this given level
        //
        //  else, then the production rate is scaled by `prod_rate / cons_rate` since
        //  lower level bandwidth is not maximized
        double r = std::min(1.0, prod_rate / cons_rate);
        prod_rate = r * counts[i] * bandwidth_hz(quanta[i], freq_khz);
    }
    return prod_rate;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace configuration
} // namespace sim
