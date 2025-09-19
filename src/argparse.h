/*
    author: Suhas Vittal
    date:   5 September 2025
*/

#ifndef ARGPARSE_h
#define ARGPARSE_h

#include <algorithm>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
    For simplicity, we implement using a builder design pattern:
*/
class ARGPARSE
{
public:
    // name, description, pointer to variable, is required
    // we require that the value have a default constructor (which it should -- int, double or string)
    template <class T> using argument_type = std::tuple<std::string_view, std::string_view, T*, bool>;

    using int_type = int64_t;
    using float_type = double;
    using string_type = std::string;
    using flag_type = bool;
private:
    std::vector<argument_type<std::string_view>> strings;
    std::vector<argument_type<int64_t>> ints;
    std::vector<argument_type<double>> floats;
    std::vector<argument_type<bool>> flags;

    std::string usage{};
public:
    ARGPARSE() =default;

    template <class T> ARGPARSE& required(std::string_view name, std::string_view description, T*);
    template <class T> ARGPARSE& optional(std::string_view name, std::string_view description, T*, T default);
    void parse(int argc, char** argv);
private:
    template <class T> void add(std::string_view name, std::string_view description, T* ptr, bool is_required);
    template <class T> void lookup_and_set(std::vector<argument_type<T>>& values, std::string_view name, T value);
};

template <class T> T convert_string_to_type(std::string);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> ARGPARSE&
ARGPARSE::required(std::string_view name, std::string_view description, T* ptr)
{
    add(name, description, ptr, true);
    return *this;
}

template <class T> ARGPARSE&
ARGPARSE::optional(std::string_view name, std::string_view description, T* ptr, T default_val)
{
    add(name, description, ptr, false);
    *ptr = default_val;
    return *this;
}

template <class T> void
ARGPARSE::add(std::string_view name, std::string_view description, T* ptr, bool is_required)
{
    if constexpr (std::is_same<T, int_type>::value)
        ints.push_back(std::make_tuple(name, description, ptr, is_required));
    else if constexpr (std::is_same<T, float_type>::value)
        floats.push_back(std::make_tuple(name, description, ptr, is_required));
    else if constexpr (std::is_same<T, flag_type>::value)
        flags.push_back(std::make_tuple(name, description, ptr, is_required));
    else if constexpr (std::is_same<T, string_type>::value)
        strings.push_back(std::make_tuple(name, description, ptr, is_required));
    else
        throw std::runtime_error("invalid type: " + std::string(typeid(T).name()) + " for argument " + std::string(name));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> void
ARGPARSE::lookup_and_set(std::vector<argument_type<T>>& values, std::string_view name, T value)
{
    auto it = std::find_if(values.begin(), values.end(), [name] (const auto& x) { return std::get<0>(x) == name; });
    if (it == values.end())
        throw std::runtime_error("usage: " + usage + "\n\nerror: " + std::string(name) + " is not a valid argument");
    *std::get<2>(*it) = value;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> T
convert_string_to_type(std::string s)
{
    if constexpr (std::is_integral<T>::value)
        return static_cast<T>(std::stoll(s));
    else if constexpr (std::is_floating_point<T>::value)
        return static_cast<T>(std::stof(s));
    else
        return s;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

#endif  // ARGPARSE_h