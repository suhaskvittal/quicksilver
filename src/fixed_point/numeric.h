/*
    author: Suhas Vittal
    date:   20 September 2025

    Rudimentary functions I need to for big integer arithmetic. Not complete, nor efficient.
*/

#ifndef FIXED_POINT_NUMERIC_h
#define FIXED_POINT_NUMERIC_h

#include "fixed_point.h"

#include <string>

template <size_t W>
using BIGINT_TYPE = FIXED_POINT<W, uint64_t>;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <size_t W> constexpr BIGINT_TYPE<W> bigint_from_hex_string(std::string_view);

namespace bigint
{

template <size_t W> BIGINT_TYPE<W> negate(BIGINT_TYPE<W>);
template <size_t W> BIGINT_TYPE<W> add(BIGINT_TYPE<W>, BIGINT_TYPE<W>);
template <size_t W> BIGINT_TYPE<W> sub(BIGINT_TYPE<W>, BIGINT_TYPE<W>);
template <size_t W> BIGINT_TYPE<W> mul(BIGINT_TYPE<W>, BIGINT_TYPE<W>);

// `div` returns both the quotient and the remainder
template <size_t W> std::pair<BIGINT_TYPE<W>, BIGINT_TYPE<W>> div(BIGINT_TYPE<W>, BIGINT_TYPE<W>);

}   // namespace bigint

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#include "numeric.tpp"

#endif  // FIXED_POINT_NUMERIC_h