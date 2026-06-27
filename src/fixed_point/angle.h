/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef FIXED_POINT_ANGLE_h
#define FIXED_POINT_ANGLE_h

#include "fixed_point.h"

#include <string>

template <size_t W>
using FPAType = FixedPoint<W, uint64_t>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W=512> constexpr FPAType<W>  convert_float_to_fpa(double, double tol=1e-18);
template <size_t W>     constexpr double       convert_fpa_to_float(const FPAType<W>&);

namespace fpa
{

template <size_t W> constexpr void negate_inplace(FPAType<W>&);
template <size_t W> constexpr void add_inplace(FPAType<W>&, FPAType<W>);
template <size_t W> constexpr void sub_inplace(FPAType<W>&, FPAType<W>);
template <size_t W> constexpr void scalar_mul_inplace(FPAType<W>&, int64_t);

template <size_t W> constexpr FPAType<W> negate(FPAType<W>);
template <size_t W> constexpr FPAType<W> add(FPAType<W>, FPAType<W>);
template <size_t W> constexpr FPAType<W> sub(FPAType<W>, FPAType<W>);
template <size_t W> constexpr FPAType<W> scalar_mul(FPAType<W>, int64_t);

enum class StringFormat { PRETTY, GRIDSYNTH, FORCE_DECIMAL, GRIDSYNTH_CPP };

template <size_t W> std::string to_string(const FPAType<W>&, StringFormat=StringFormat::PRETTY);

} // namespace fpa

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "angle.tpp"

#endif
