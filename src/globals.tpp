/*
 *  author: Suhas Vittal
 *  date:   5 January 2026
 * */

#include <iomanip>
#include <iostream>

template <class T> void
print_stat_line(std::ostream& out, std::string_view name, T value)
{
    out << std::setw(64) << std::left << name;
    if constexpr (std::is_floating_point<T>::value)
        out << std::setw(12) << std::right << std::fixed << std::setprecision(3) << value;
    else
        out << std::setw(12) << std::right << value;
    out << "\n";
}

template <class T> double 
mean(T x, T y) 
{ 
    return static_cast<double>(x) / static_cast<double>(y); 
}
