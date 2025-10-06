/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef FIXED_POINT_ANGLE_h
#define FIXED_POINT_ANGLE_h

#include "fixed_point.h"

#include <string>

template <size_t W>
using FPA_TYPE = FIXED_POINT<W, uint64_t>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W=512> FPA_TYPE<W>  convert_float_to_fpa(double, double tol=1e-18);
template <size_t W>     double       convert_fpa_to_float(const FPA_TYPE<W>&);

namespace fpa
{

template <size_t W> void negate_inplace(FPA_TYPE<W>&);
template <size_t W> void add_inplace(FPA_TYPE<W>&, FPA_TYPE<W>);
template <size_t W> void sub_inplace(FPA_TYPE<W>&, FPA_TYPE<W>);
template <size_t W> void scalar_mul_inplace(FPA_TYPE<W>&, int64_t);

template <size_t W> FPA_TYPE<W> negate(FPA_TYPE<W>);
template <size_t W> FPA_TYPE<W> add(FPA_TYPE<W>, FPA_TYPE<W>);
template <size_t W> FPA_TYPE<W> sub(FPA_TYPE<W>, FPA_TYPE<W>);
template <size_t W> FPA_TYPE<W> scalar_mul(FPA_TYPE<W>, int64_t);

enum class STRING_FORMAT { PRETTY, GRIDSYNTH, FORCE_DECIMAL, GRIDSYNTH_CPP };

template <size_t W> std::string to_string(const FPA_TYPE<W>&, STRING_FORMAT=STRING_FORMAT::PRETTY);

} // namespace fpa

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "angle.tpp"

#endif