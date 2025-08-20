/*
    author: Suhas Vittal
    date:   19 August 2025
*/

#ifndef FIXED_POINT_ANGLE_h
#define FIXED_POINT_ANGLE_h

#include "fixed_point.h"

template <size_t W>
using fpa_type = FIXED_POINT<W, uint64_t>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W=512> fpa_type<W>  convert_float_to_fpa(double);
template <size_t W>     double       convert_fpa_to_float(const fpa_type<W>&);

namespace fpa
{

template <size_t W> void negate_inplace(fpa_type<W>&);
template <size_t W> void add_inplace(fpa_type<W>&, fpa_type<W>);
template <size_t W> void sub_inplace(fpa_type<W>&, fpa_type<W>);

template <size_t W> fpa_type<W> negate(fpa_type<W>);
template <size_t W> fpa_type<W> add(fpa_type<W>, fpa_type<W>);
template <size_t W> fpa_type<W> sub(fpa_type<W>, fpa_type<W>);

} // namespace fpa

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "angle.tpp"

#endif