/*
 *  author: Suhas Vittal
 *  date:   1 July 2026
 * */

#include <iostream>
#include <sstream>

#define TEMPL_PARAMS template <class T>
#define TEMPL_CLASS  Histogram<T>

namespace stats
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

TEMPL_PARAMS
TEMPL_CLASS::Histogram(std::string_view _name, T lwr, T upp, size_t _buckets)
    :name(_name),
    range_min(lwr),
    range_max(upp),
    bucket_width((upp-lwr)/_buckets),
    buckets(_buckets),
    counts_(buckets+2, 0)
{}

TEMPL_PARAMS
TEMPL_CLASS::Histogram(const Histogram& h)
    :name(h.name),
    range_min(h.range_min),
    range_max(h.range_max),
    bucket_width(h.bucket_width),
    buckets(h.buckets),
    sum_(h.sum_),
    sum_sq_(h.sum_sq_),
    min_(h.min_),
    max_(h.max_),
    counts_(h.counts_),
    total_(h.total_)
{}

TEMPL_PARAMS template <class U> void
TEMPL_CLASS::add(U _x)
{
    const T x = static_cast<T>(_x);
    size_t idx;
    if (x < range_min)
        idx = underflow_idx();
    else if (x >= range_max)
        idx = overflow_idx();
    else
        idx = (x-range_min) / bucket_width;
    sum_ += x;
    sum_sq_ += x*x;
    counts_[idx]++;
    total_++;
    min_ = std::min(min_, x);
    max_ = std::max(max_, x);
}

TEMPL_PARAMS void
TEMPL_CLASS::dump(std::ostream& ostrm, size_t indent_level) const
{
    if (total() == 0)
        return;
    std::string ind;
    ind.reserve(4*indent_level);
    for (size_t i = 0; i < 4*indent_level; i++)
        ind.push_back(' ');


    ostrm << ind << name << "\n";
    print_stat_line(ostrm, ind + "    COUNT", total());
    print_stat_line(ostrm, ind + "    MEAN", mean());
    print_stat_line(ostrm, ind + "    STD", std());
    print_stat_line(ostrm, ind + "    MIN", min());
    print_stat_line(ostrm, ind + "    MAX", max());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace stats

#undef TEMPL_PARAMS
#undef TEMPL_CLASS
