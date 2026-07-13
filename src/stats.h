/*
 *  author: Suhas Vittal
 *  date:   1 July 2026
 * */

#ifndef STATS_h
#define STATS_h

#include "globals.h"

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace stats
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T>
class Histogram
{
public:
    const std::string name;
    const T range_min,
            range_max,
            bucket_width;
    const size_t buckets;
private:
    T sum_{},
      sum_sq_{},
      min_{ std::numeric_limits<T>::max() },
      max_{};

    std::vector<size_t> counts_;
    size_t total_{};
public:
    Histogram(std::string_view name, T lwr, T upp, size_t buckets);
    Histogram(const Histogram&);

    template <class U>
    void add(U);

    double mean() const { return fpdiv(sum_, total_); }
    double std() const { return std::sqrt(fpdiv(sum_sq_, total_) - mean()); }
    T min() const { return min_; }
    T max() const { return max_; }
    size_t total() const { return total_; }

    void dump(std::ostream&, size_t indent=0) const; 
private:
    size_t underflow_idx() const { return buckets; }
    size_t overflow_idx() const { return buckets+1; }
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace stats

#include "stats.tpp"

#endif
