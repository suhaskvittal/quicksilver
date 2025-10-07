/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#ifndef COMPILER_PROGRAM_ROTATION_MANAGER_h
#define COMPILER_PROGRAM_ROTATION_MANAGER_h

#include "instruction.h"

#include <cmath>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

struct COMPARABLE_FLOAT
{
    double value;
    ssize_t precision;

    bool
    operator==(const COMPARABLE_FLOAT& other) const
    {
        return -log10(fabsl(value - other.value)) 
                > std::max(static_cast<double>(precision), static_cast<double>(other.precision))-2;
    }
};

}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// Specializations of `std::hash`
namespace std
{

template <>
struct hash<INSTRUCTION::fpa_type>
{
    using value_type = INSTRUCTION::fpa_type;

    size_t
    operator()(const value_type& x) const 
    { 
        const auto& words = x.get_words();
        uint64_t out = std::reduce(words.begin(), words.end(), uint64_t{0},
                            [] (uint64_t acc, uint64_t word) { return acc ^ word; });
        return out;
    }
};

template <>
struct hash<prog::COMPARABLE_FLOAT>
{
    using value_type = prog::COMPARABLE_FLOAT;
    
    size_t
    operator()(const value_type& x) const
    {
        return std::hash<double>{}(x.value) ^ std::hash<size_t>{}(x.precision);
    }
};

}   // namespace std

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void rotation_manager_init(size_t num_threads=8);
void rotation_manager_end();

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// multi-threaded functions:
void                           rm_schedule_synthesis(const INSTRUCTION::fpa_type&, ssize_t precision);
std::vector<INSTRUCTION::TYPE> rm_find(const INSTRUCTION::fpa_type&, ssize_t precision);
void                           rm_thread_iteration();

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class BASIS_TYPE { X, Z, NONE };

// passes to modify the rotation sequence and condense it:
void rm_flip_h_subsequences(std::vector<INSTRUCTION::TYPE>&);
void rm_consolidate_and_reduce_subsequences(std::vector<INSTRUCTION::TYPE>&);

// this is the main function that synthesizes the rotation
std::vector<INSTRUCTION::TYPE> rm_synthesize_rotation(const INSTRUCTION::fpa_type&, ssize_t precision);

// this is a helper function for `consolidate_and_reduce_subsequences` that
// sets the gates starting from `begin` to the appropriate gates that implement the given rotation
// 
// returns the iterator to the next gate after the last modified gate -- these should be removed.
std::vector<INSTRUCTION::TYPE>::iterator 
    rm_consolidate_gate(BASIS_TYPE, int8_t rotation_sum, std::vector<INSTRUCTION::TYPE>::iterator begin);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// utility functions (constexpr or templated):

template <class ITERABLE> std::string
urotseq_to_string(ITERABLE iterable)
{
    std::stringstream strm;
    bool first{true};
    for (auto x : iterable)
    {
        if (!first)
            strm << "'";
        first = false;
        std::string_view sx = BASIS_GATES[static_cast<size_t>(x)];
        strm << sx;
    }
    return strm.str();
}

constexpr INSTRUCTION::TYPE
flip_basis(INSTRUCTION::TYPE g)
{
    switch (g)
    {
        case INSTRUCTION::TYPE::Z:
            return INSTRUCTION::TYPE::X;
        case INSTRUCTION::TYPE::S:
            return INSTRUCTION::TYPE::SX;
        case INSTRUCTION::TYPE::SDG:
            return INSTRUCTION::TYPE::SXDG;
        case INSTRUCTION::TYPE::T:
            return INSTRUCTION::TYPE::TX;
        case INSTRUCTION::TYPE::TDG:
            return INSTRUCTION::TYPE::TXDG;

        case INSTRUCTION::TYPE::X:
            return INSTRUCTION::TYPE::Z;
        case INSTRUCTION::TYPE::SX:
            return INSTRUCTION::TYPE::S;
        case INSTRUCTION::TYPE::SXDG:
            return INSTRUCTION::TYPE::SDG;
        case INSTRUCTION::TYPE::TX:
            return INSTRUCTION::TYPE::T;
        case INSTRUCTION::TYPE::TXDG:
            return INSTRUCTION::TYPE::TDG;

        default:
            return g;
    }
}

constexpr BASIS_TYPE
get_basis_type(INSTRUCTION::TYPE g)
{
    switch (g)
    {
        case INSTRUCTION::TYPE::X:
        case INSTRUCTION::TYPE::SX:
        case INSTRUCTION::TYPE::SXDG:
        case INSTRUCTION::TYPE::TX:
        case INSTRUCTION::TYPE::TXDG:
            return BASIS_TYPE::X;

        case INSTRUCTION::TYPE::Z:
        case INSTRUCTION::TYPE::S:
        case INSTRUCTION::TYPE::SDG:
        case INSTRUCTION::TYPE::T:
        case INSTRUCTION::TYPE::TDG:
            return BASIS_TYPE::Z;

        default:
            return BASIS_TYPE::NONE;
    }
}

constexpr int8_t
get_rotation_value(INSTRUCTION::TYPE g)
{
    // the output is r, where g is some rotation of r*pi/4
    switch (g)
    {
    case INSTRUCTION::TYPE::X:
    case INSTRUCTION::TYPE::Z:
        return 4;
    case INSTRUCTION::TYPE::S:
    case INSTRUCTION::TYPE::SX:
        return 2;
    case INSTRUCTION::TYPE::SDG:
    case INSTRUCTION::TYPE::SXDG:
        return 6;
    case INSTRUCTION::TYPE::T:
    case INSTRUCTION::TYPE::TX:
        return 1;
    case INSTRUCTION::TYPE::TDG:
    case INSTRUCTION::TYPE::TXDG:
        return 7;
    default:
        return -1;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace prog

#endif