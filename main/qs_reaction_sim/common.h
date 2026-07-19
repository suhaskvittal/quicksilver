/*
 * author: Suhas Vittal
 * date:    11 July 2026
 * */

#ifndef RS_COMMON_h
#define RS_COMMON_h

#include "globals.h"

namespace rs
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * These two functions update the given history using
 * the passed in data structure. It is assumed that
 * `array[i]` or (`map[i]` if using the map variant) returns
 * the next available cycle for qubit `i`.
 * */

template <class HistoryPtr, class ArrayType>
void add_idle_cycle_array(HistoryPtr&, cycle_type current_cycle, const ArrayType&);

template <class HistoryPtr, class MapType>
void add_idle_cycle_map(HistoryPtr&, cycle_type current_cycle, const MapType&);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace rs

#include "common.tpp"

#endif
