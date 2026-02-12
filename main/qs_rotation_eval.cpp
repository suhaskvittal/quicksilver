/*
 *  author: Suhas Vittal
 *  date:   11 February 2026
 * */

#include "argparse.h"
#include "compiler/program/rotation_manager.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <sstream>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using fpa_type = INSTRUCTION::fpa_type;
using urotseq_type = INSTRUCTION::urotseq_type;

using amp_type = std::complex<double>;
using state_type = std::array<amp_type, 2>;

/*
 * Returns the required gridsynth precision to accurately approximate
 * the given angle.
 * */
size_t get_required_precision(const fpa_type&);

/*
 * Validates the given urotseq and compares it to the given angle.
 * Returns true if the approximation is sufficiently accurate.
 *
 * The `apply_*()` functions below are helpers for `validate_urotseq`
 * */
bool validate_urotseq(const urotseq_type&, const fpa_type&);
void apply_gate(state_type&, INSTRUCTION::TYPE);
void apply_h_gate(state_type&);
void apply_z_rotation(state_type&, int8_t degree);  // degree: 1 = T, 2 = S, 4 = Z

std::string as_polar(amp_type);

/*
 * Useful constants:
 * */
const amp_type   _1_D_RT_2(1.0/std::sqrt(2.0), 0.0);
const state_type INITIAL_STATE{_1_D_RT_2, _1_D_RT_2};  // initialize in |+> state.

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    int64_t     num_angles;
    int64_t     num_threads;
    std::string output;

    ARGPARSE()
        .optional("-n", "--num-angles",  "Angles per region",            num_angles,  10000)
        .optional("-t", "--num-threads", "Number of synthesis threads",   num_threads, 8)
        .optional("-o", "--output",      "Output file (.bin or .bin.gz)", output,      std::string("rotations.bin"))
        .parse(argc, argv);

    const size_t total = 16 * static_cast<size_t>(num_angles);

    std::cout << "=== Rotation Lookup Table Generator ===\n"
              << "Regions: 16, Angles/region: " << num_angles
              << ", Total: " << total
              << ", Threads: " << num_threads
              << ", Output: " << output << "\n\n";

    /*
     * Process regions in batches of 3 to bound peak memory usage.
     * Within each batch: generate angles, schedule all syntheses upfront
     * (non-blocking), then retrieve and write each result as it completes.
     * File I/O overlaps with synthesis of later entries within the batch.
     *
     * Per-entry binary layout:
     *   [1B  word_count]
     *   [8B * word_count  fpa words]
     *   [2B  seq_len]
     *   [1B * seq_len     gate bytes]
     * */
    constexpr size_t REGIONS_PER_BATCH = 1;
    constexpr size_t TOTAL_REGIONS     = 12;

    auto fill_region = [&](std::vector<fpa_type>& ang, std::vector<size_t>& prec,
                           size_t base, double lo, double hi)
    {
        double step = (hi - lo) / static_cast<double>(num_angles);
        for (int64_t i = 0; i < num_angles; i++)
        {
            double v = lo + (i + 0.5) * step;
            ang[base + i] = convert_float_to_fpa<INSTRUCTION::FPA_PRECISION>(v);
            prec[base + i] = get_required_precision(ang[base + i]);
        }
    };

    generic_strm_type strm;
    generic_strm_open(strm, output, "wb");

    size_t written = 0;
    auto last_print = std::chrono::steady_clock::now();

    for (size_t batch_start = 0; batch_start < TOTAL_REGIONS; batch_start += REGIONS_PER_BATCH)
    {
        prog::rotation_manager_init(num_threads);

        size_t batch_end     = std::min(batch_start + REGIONS_PER_BATCH, TOTAL_REGIONS);
        size_t batch_total   = (batch_end - batch_start) * static_cast<size_t>(num_angles);

        std::vector<fpa_type> angles(batch_total);
        std::vector<size_t>   precisions(batch_total);

        for (int r = batch_start; r < batch_end; r++)
        {
            size_t local_base = (r - batch_start) * num_angles;
            if (r == 0)
                fill_region(angles, precisions, local_base, 0.0, 2.0 * M_PI);
            else
                fill_region(angles, precisions, local_base, std::pow(10.0, -r), std::pow(10.0, -(r-1)));
        }

        for (size_t i = 0; i < batch_total; i++)
            prog::rotation_manager_schedule_synthesis(angles[i], precisions[i]);

        std::cout << "B" << batch_start << " :\t";
        for (size_t i = 0; i < batch_total; i++)
        {
            if (i % (num_angles/100) == 0)
                (std::cout << ".").flush();
            auto urotseq = prog::rotation_manager_find(angles[i], precisions[i]);

            uint8_t  word_count = static_cast<uint8_t>(fpa_type::NUM_WORDS);
            auto     words      = angles[i].get_words_ref();
            uint16_t seq_len    = static_cast<uint16_t>(urotseq.size());

            generic_strm_write(strm, &word_count, 1);
            generic_strm_write(strm, (void*)&words[0], sizeof(fpa_type::word_type)*fpa_type::NUM_WORDS);
            generic_strm_write(strm, &seq_len, 2);

            for (auto gate : urotseq)
            {
                uint8_t g = static_cast<uint8_t>(gate);
                generic_strm_write(strm, &g, 1);
            }

            written++;
        }
        std::cout << "\n";

        prog::rotation_manager_end(true);
    }

    generic_strm_close(strm);
    prog::rotation_manager_end();

    std::cout << "Wrote " << total << " entries to " << output << "\n";
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
get_required_precision(const fpa_type& angle)
{
    /*
    size_t msb = angle.join_word_and_bit_idx(angle.msb());
    if (msb == fpa_type::NUM_BITS-1)
        msb = angle.join_word_and_bit_idx(fpa::negate(angle).msb());
    msb = fpa_type::NUM_BITS - msb - 1;
    return (msb/3) + 3;
    */
    return static_cast<size_t>( std::round(-std::log10(std::abs(convert_fpa_to_float(angle)))) + 3 );
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
validate_urotseq(const urotseq_type& s, const fpa_type& angle)
{
    // apply gates:
    state_type q(INITIAL_STATE);
    for (auto g : s)
        apply_gate(q, g);

    // handle global phase:
    const double s0_phase = std::arg(q[0]),
                 s1_phase = std::arg(q[1]);
    double computed_angle = s1_phase - s0_phase;
    while (computed_angle < 0)
        computed_angle += 2*M_PI;
    // check if the precision is good:
    const double true_angle = convert_fpa_to_float(angle);
    size_t p = get_required_precision(angle);

    bool ok = std::abs(true_angle - computed_angle) < std::pow(10.0, -static_cast<int>(p-1));
    if (!ok)
    {
        std::cerr << "\033[1;31m"
                << "urotseq for angle " << fpa::to_string(angle)
                << " was incorrect: got " << computed_angle
                << ", expected " << true_angle
                << ", tol = " << p
                << "\n\tfinal state = [ " << as_polar(q[0]) << " , " << as_polar(q[1]) << " ]"
                << "\033[0m" << "\n";
    }
    return ok;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
apply_gate(state_type& q, INSTRUCTION::TYPE g)
{
    if (g == INSTRUCTION::TYPE::H)
    {
        apply_h_gate(q);
        return;
    }

    int8_t r_degree;
    if (g == INSTRUCTION::TYPE::Z || g == INSTRUCTION::TYPE::X)
        r_degree = 4;
    else if (g == INSTRUCTION::TYPE::T || g == INSTRUCTION::TYPE::TX)
        r_degree = 1;
    else if (g == INSTRUCTION::TYPE::S || g == INSTRUCTION::TYPE::SX)
        r_degree = 2;
    else if (g == INSTRUCTION::TYPE::SDG || g == INSTRUCTION::TYPE::SXDG)
        r_degree = 6;
    else
        r_degree = 7;  // TDG or TXDG

    const bool is_x_basis = (g == INSTRUCTION::TYPE::X)
                                || (g == INSTRUCTION::TYPE::SX)
                                || (g == INSTRUCTION::TYPE::SXDG)
                                || (g == INSTRUCTION::TYPE::TX)
                                || (g == INSTRUCTION::TYPE::TXDG);
    if (is_x_basis)
        apply_h_gate(q);
    apply_z_rotation(q, r_degree);
    if (is_x_basis)
        apply_h_gate(q);
}

void
apply_h_gate(state_type& q)
{
    state_type p{};
    p[0] = _1_D_RT_2 * (q[0] + q[1]);
    p[1] = _1_D_RT_2 * (q[0] - q[1]);
    q = std::move(p);
}

void
apply_z_rotation(state_type& q, int8_t degree)
{
    using namespace std::complex_literals;

    double d = static_cast<double>(degree) / 8;
    q[1] *= std::exp(2i * M_PI * d);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
as_polar(amp_type x)
{
    double r = std::abs(x),
           t = std::arg(x);
    std::stringstream ss;
    ss << std::scientific << r << " @ " << t;
    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
