/*
 *  author: Suhas Vittal
 *  date:   11 February 2026
 * */

#include "compiler/program/rotation_synthesis.h"

#include "nwqec/gridsynth/gridsynth.hpp"

namespace compiler
{
namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using urotseq_type = Instruction::urotseq_type;
using fpa_type = Instruction::fpa_type;
using amp_type = std::complex<double>;
using state_type = std::array<amp_type, 2>;

enum Basis { X, Z, NONE };

/*
 * Flips the basis of all gates sandwiched by two H gates. For example, H*T*S*H --> TX*SX
 *
 * This is the first optimization pass in TACO.
 * */
void _flip_h_subsequences(urotseq_type&);

/*
 * Consolidation involves merging gates in the same basis into one or two gates (only
 * one non-software gate is used).
 *
 * This is the second optimization pass in TACO.
 * */
void _consolidate_and_reduce_subsequences(urotseq_type&);

/*
 * This function overwrites the data starting from `begin` inplace (by modifying the gate types).
 * Then, it returns an iterator right after the last modified entry.
 * */
void _consolidate_gate(Basis, int8_t rotation_sum, urotseq_type::iterator begin, urotseq_type::iterator end);

/*
 * Returns the basis (X or Z or None) for the given gate.
 * */
constexpr Basis _get_basis_type(Instruction::Type g);

/*
 * Flips the basis of the given gate. For example, T --> TX or vice versa.
 * */
constexpr Instruction::Type _flip_basis(Instruction::Type g);

/*
 * `_get_rotation_value` quantizes the "rotation" of `g` to a 3-bit value.
 * 1 = pi/4 rotation (T-like gates),
 * 2 = pi/2 rotation (S-like gates)
 * 4 = pi rotations (X or Z)
 * */
constexpr int8_t _get_rotation_value(Instruction::Type g);

/*
 * These `_apply_*()` functions are helpers for `validate_urotseq()`
 * */
void _apply_gate(state_type&, Instruction::Type);
void _apply_h_gate(state_type&);
void _apply_z_rotation(state_type&, int8_t rotation_sum);  // degree: 1 = T, 2 = S, 4 = Z

/*
 * Useful constants:
 * */
const amp_type   _1_D_RT_2(1.0/std::sqrt(2.0), 0.0);
const state_type INITIAL_STATE{_1_D_RT_2, _1_D_RT_2};  // initialize in |+> state.

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

urotseq_type
synthesize_rotation(const fpa_type& rotation, ssize_t precision, bool verbose)
{
    // call gridsynth:
    std::string fpa_str = fpa::to_string(rotation, fpa::StringFormat::GRIDSYNTH_CPP);
    std::string epsilon = "1e-" + std::to_string(precision);

    auto [gates_str, t_ms] = gridsynth::gridsynth_gates(
                                    fpa_str,
                                    epsilon,
                                    NWQEC::DEFAULT_DIOPHANTINE_TIMEOUT_MS,
                                    NWQEC::DEFAULT_FACTORING_TIMEOUT_MS,
                                    false,
                                    false);

    if (verbose && t_ms > 5000.0)
    {
        std::cerr << "synthesize_rotation: possible performance issue: gridsynth took " 
            << t_ms << " ms for inputs: " << fpa_str
            << ", epsilon: " << epsilon << " (b = " << precision
            << "), fpa hex = " << rotation.to_hex_string() << "\n";
    }

    urotseq_type out;
    for (char c : gates_str)
    {
        if (c == 'H')
            out.push_back(Instruction::Type::H);
        else if (c == 'T')
            out.push_back(Instruction::Type::T);
        else if (c == 'X')
            out.push_back(Instruction::Type::X);
        else if (c == 'Z')
            out.push_back(Instruction::Type::Z);
        else if (c == 'S')
            out.push_back(Instruction::Type::S);
    }

    _flip_h_subsequences(out);
    _consolidate_and_reduce_subsequences(out);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
validate_urotseq(const urotseq_type& x, const fpa_type& angle, ssize_t precision)
{
    // apply gates:
    state_type q(INITIAL_STATE);
    for (auto g : x)
        _apply_gate(q, g);

    // handle global phase:
    const double s0_phase = std::arg(q[0]),
                 s1_phase = std::arg(q[1]);
    double computed_angle = s1_phase - s0_phase;
    while (computed_angle < 0)
        computed_angle += 2*M_PI;
    // check if the precision is good:
    const double true_angle = convert_fpa_to_float(angle);
    const double eps = std::pow(10.0, -static_cast<int>(precision));
    bool ok = std::abs(true_angle - computed_angle) < eps;
    if (!ok)
    {
        std::cerr << "\033[1;31m"
                << "urotseq for angle " << fpa::to_string(angle)
                << " was incorrect: got " << computed_angle
                << ", expected " << true_angle
                << ", precision = " << precision << " (eps = " << eps << ")"
               << "\033[0m" << "\n";
    }
    return ok;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_flip_h_subsequences(urotseq_type& urotseq)
{
    size_t h_count = std::count(urotseq.begin(), urotseq.end(), Instruction::Type::H);

    auto begin = urotseq.begin();
    // while there are at least two H gates, flip the subsequence between them:
    while (h_count >= 2)
    {
        auto h_begin = std::find(begin, urotseq.end(), Instruction::Type::H);
        auto h_end = std::find(h_begin+1, urotseq.end(), Instruction::Type::H);
        std::for_each(h_begin+1, h_end, [](auto& g) { g = _flip_basis(g); });

        // set the H gates to nil -- we will remove all NIL gates at the end:
        *h_begin = Instruction::Type::NIL;
        *h_end = Instruction::Type::NIL;

        begin = h_end+1;
        h_count -= 2;
    }

    if (h_count == 1)
    {
        // the last H gate can be propagated to the end by flipping everything between:
        auto h_begin = std::find(begin, urotseq.end(), Instruction::Type::H);
        std::for_each(h_begin+1, urotseq.end(), [] (auto& g) { g = _flip_basis(g); });
        std::move(h_begin+1, urotseq.end(), h_begin);
        urotseq.back() = Instruction::Type::H;
    }

    auto it = std::remove(urotseq.begin(), urotseq.end(), Instruction::Type::NIL);
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_consolidate_and_reduce_subsequences(urotseq_type& urotseq)
{
    // generate a subsequence, stop until we hit an H gate or gate in a different basis:
    Basis current_basis{Basis::NONE};
    int8_t current_rotation_sum{0};
    auto seq_begin = urotseq.begin();
    for (auto it = urotseq.begin(); it != urotseq.end(); it++)
    {
        auto g = *it;
        if (current_basis != Basis::NONE)
        {
            if (_get_basis_type(g) != current_basis)
            {
                _consolidate_gate(current_basis, current_rotation_sum, seq_begin, it);
                current_basis = Basis::NONE;
                current_rotation_sum = 0;
            }
            else
            {
                current_rotation_sum += _get_rotation_value(g);
                current_rotation_sum &= 7;  // mod 8
            }
        }

        // this is not an else since we may set `current_basis` to `Basis::NONE` in the above if statement
        if (current_basis == Basis::NONE)
        {
            if (g == Instruction::Type::H)
                continue;  // nothing to be done

            current_basis = _get_basis_type(g);
            assert(current_basis != Basis::NONE);
            current_rotation_sum = _get_rotation_value(g);
            seq_begin = it;
        }
    }

    // if we are still in a subsequence, finish it off:
    if (current_basis != Basis::NONE)
        _consolidate_gate(current_basis, current_rotation_sum, seq_begin, urotseq.end());

    auto it = std::remove(urotseq.begin(), urotseq.end(), Instruction::Type::NIL);
    urotseq.erase(it, urotseq.end());
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_consolidate_gate(Basis basis, int8_t rotation_sum, urotseq_type::iterator begin, urotseq_type::iterator end)
{
    if (rotation_sum == 0)
    {
        std::fill(begin, end, Instruction::Type::NIL);
        return;
    }

    bool is_z = basis == Basis::Z;
    if (rotation_sum == 1 || rotation_sum == 5)
        *begin = is_z ? Instruction::Type::T : Instruction::Type::TX;
    else if (rotation_sum == 2)
        *begin = is_z ? Instruction::Type::S : Instruction::Type::SX;
    else if (rotation_sum == 4)
        *begin = is_z ? Instruction::Type::Z : Instruction::Type::X;
    else if (rotation_sum == 6)
        *begin = is_z ? Instruction::Type::SDG : Instruction::Type::SXDG;
    else if (rotation_sum == 3 || rotation_sum == 7)
        *begin = is_z ? Instruction::Type::TDG : Instruction::Type::TXDG;
    begin++;

    // if 3 or 5, add an extra pi rotation
    if (rotation_sum == 3 || rotation_sum == 5)
    {
        *begin = is_z ? Instruction::Type::Z : Instruction::Type::X;
        begin++;
    }
    
    // remainder of sequence is now invalid (so set to NIL to mark for deletion)
    std::fill(begin, end, Instruction::Type::NIL);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr Basis
_get_basis_type(Instruction::Type g)
{
    switch (g)
    {
    case Instruction::Type::X:
    case Instruction::Type::SX:
    case Instruction::Type::SXDG:
    case Instruction::Type::TX:
    case Instruction::Type::TXDG:
        return Basis::X;

    case Instruction::Type::Z:
    case Instruction::Type::S:
    case Instruction::Type::SDG:
    case Instruction::Type::T:
    case Instruction::Type::TDG:
        return Basis::Z;

    default:
        return Basis::NONE;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr Instruction::Type
_flip_basis(Instruction::Type g)
{
    switch (g)
    {
    case Instruction::Type::Z:
        return Instruction::Type::X;
    case Instruction::Type::S:
        return Instruction::Type::SX;
    case Instruction::Type::SDG:
        return Instruction::Type::SXDG;
    case Instruction::Type::T:
        return Instruction::Type::TX;
    case Instruction::Type::TDG:
        return Instruction::Type::TXDG;

    case Instruction::Type::X:
        return Instruction::Type::Z;
    case Instruction::Type::SX:
        return Instruction::Type::S;
    case Instruction::Type::SXDG:
        return Instruction::Type::SDG;
    case Instruction::Type::TX:
        return Instruction::Type::T;
    case Instruction::Type::TXDG:
        return Instruction::Type::TDG;

    default:
        return g;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

constexpr int8_t
_get_rotation_value(Instruction::Type g)
{
    // the output is r, where g is some rotation of r*pi/4
    switch (g)
    {
    case Instruction::Type::X:
    case Instruction::Type::Z:
        return 4;
    case Instruction::Type::S:
    case Instruction::Type::SX:
        return 2;
    case Instruction::Type::SDG:
    case Instruction::Type::SXDG:
        return 6;
    case Instruction::Type::T:
    case Instruction::Type::TX:
        return 1;
    case Instruction::Type::TDG:
    case Instruction::Type::TXDG:
        return 7;
    default:
        return -1;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_apply_gate(state_type& q, Instruction::Type g)
{
    if (g == Instruction::Type::H)
    {
        _apply_h_gate(q);
        return;
    }

    const bool is_x_basis = _get_basis_type(g) == Basis::X;
    if (is_x_basis)
        _apply_h_gate(q);
    _apply_z_rotation(q, _get_rotation_value(g));
    if (is_x_basis)
        _apply_h_gate(q);
}

void
_apply_h_gate(state_type& q)
{
    state_type p{};
    p[0] = _1_D_RT_2 * (q[0] + q[1]);
    p[1] = _1_D_RT_2 * (q[0] - q[1]);
    q = std::move(p);
}

void
_apply_z_rotation(state_type& q, int8_t degree)
{
    using namespace std::complex_literals;

    double d = static_cast<double>(degree) / 8;
    q[1] *= std::exp(2i * M_PI * d);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog
} // namespace compiler
