/*
 *  author: Claude (Opus 4.8)
 *  date:   14 June 2026
 *
 *  Compares the synthesis cost of `Rz(x)` against `Rz(2x)` for every rotation
 *  stored in the LUT files under `rotation_data/`.
 *
 *  For each stored angle `x`, the `Rz(x)` synthesis is the sequence kept in the
 *  LUT file itself. The `Rz(2x)` synthesis is obtained the same way the compiler
 *  would synthesize an arbitrary angle: by doubling the fixed-point angle and
 *  looking up the nearest stored entry via the rotation manager.
 *
 *  The result is dumped as a CSV file (one row per angle) for plotting in
 *  `scripts/rdr_plots.ipynb`.
 * */

#include "argparse/argparse.h"
#include "compiler/program/rotation_manager.h"
#include "fixed_point/angle.h"
#include "generic_io.h"
#include "globals.h"
#include "instruction.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using fpa_type = INSTRUCTION::fpa_type;
using urotseq_type = INSTRUCTION::urotseq_type;

constexpr size_t LUT_COUNT_PER_SIGN{12};

/*
 * The rotation manager only resolves angles whose magnitude lies in
 * (1e-11, 2*pi) (see `_get_lut_array_idx` in rotation_manager.cpp). Doubling an
 * angle near a multiple of pi can wrap it to a tiny non-zero value that falls
 * below this range; such angles are skipped rather than aborting the run.
 * */
constexpr double MIN_RESOLVABLE_MAGNITUDE{1e-11};

struct lut_entry
{
    double       angle{};
    urotseq_type urotseq;
};

/*
 * Reads a single LUT file. The format mirrors `_read_lut_from_file` in
 * src/compiler/program/rotation_manager.cpp:
 *  - 8B floating point angle
 *  - 2B urotseq byte count
 *  - urotseq data (one byte per gate, cast to INSTRUCTION::TYPE)
 * */
std::vector<lut_entry>
read_lut_file(const std::string& file_path)
{
    // Matches UROTSEQ_CAPACITY in rotation_manager.cpp; the longest stored
    // synthesis in rotation_data is ~304 gates.
    constexpr size_t UROTSEQ_CAPACITY{512};

    generic_strm_type strm;
    generic_strm_open(strm, file_path, "rb");

    std::vector<lut_entry> out;

    double   angle;
    uint16_t urotseq_byte_count;
    uint8_t  urotseq_bytes[UROTSEQ_CAPACITY];

    while (!generic_strm_eof(strm))
    {
        // Guard every read against a short/empty read at EOF: the decompressed
        // stream can report `!eof()` for one extra iteration, and reading into a
        // stale `urotseq_byte_count` would overflow `urotseq_bytes`.
        if (generic_strm_read(strm, &angle, sizeof(angle)) != sizeof(angle))
            break;
        if (generic_strm_read(strm, &urotseq_byte_count, sizeof(urotseq_byte_count)) != sizeof(urotseq_byte_count))
            break;
        if (urotseq_byte_count > UROTSEQ_CAPACITY)
            break;
        if (generic_strm_read(strm, urotseq_bytes, urotseq_byte_count) != urotseq_byte_count)
            break;

        urotseq_type urotseq(urotseq_byte_count);
        std::transform(urotseq_bytes, urotseq_bytes+urotseq_byte_count, urotseq.begin(),
                        [] (uint8_t b) { return static_cast<INSTRUCTION::TYPE>(b); });

        out.push_back(lut_entry{angle, std::move(urotseq)});
    }

    generic_strm_close(strm);
    return out;
}

/*
 * Number of T-like gates (T/TX/TDG/TXDG) in a synthesis sequence.
 * */
size_t
count_t_gates(const urotseq_type& seq)
{
    return std::count_if(seq.begin(), seq.end(),
                        [] (auto t) { return is_t_like_instruction(t); });
}

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
#if !defined(ROTATION_SYNTHESIS_LUT_FOLDER_PATH)
    std::cerr << "qs_compare_syntheses: ROTATION_SYNTHESIS_LUT_FOLDER_PATH not defined by build system"
            << _die{};
#endif

    std::string output_file;

    ARGPARSE()
        .required("output-file", "output CSV file", output_file)
        .parse(argc, argv);

    // populates the rotation manager's lookup tables (used for the Rz(2x) side):
    compiler::prog::rotation_manager_init();

    std::ofstream out(output_file);
    out << "angle,t_count_x,t_count_2x,len_x,len_2x\n";
    out << std::setprecision(17);

    size_t rows_written{0},
           rows_skipped{0};

    for (size_t i = 0; i < LUT_COUNT_PER_SIGN; i++)
    {
        for (bool pos : {true, false})
        {
            std::string file_path{ROTATION_SYNTHESIS_LUT_FOLDER_PATH};
            file_path += (pos ? "/p" : "/n") + std::to_string(i) + ".bin.xz";

            for (const auto& e : read_lut_file(file_path))
            {
                // Rz(2x): double the fixed-point angle (mod 2*pi) and look up its
                // synthesis, exactly as the compiler would for an arbitrary angle.
                fpa_type two_theta = fpa::scalar_mul(convert_float_to_fpa<fpa_type::NUM_BITS>(e.angle), 2);
                double f = std::abs(convert_fpa_to_float(two_theta));
                if (two_theta.popcount() != 0 && f <= MIN_RESOLVABLE_MAGNITUDE)
                {
                    // 2x wrapped to a magnitude below the resolvable LUT range.
                    rows_skipped++;
                    continue;
                }

                urotseq_type seq_2x = compiler::prog::rotation_manager_lookup(two_theta);

                out << e.angle << ','
                    << count_t_gates(e.urotseq) << ','
                    << count_t_gates(seq_2x) << ','
                    << e.urotseq.size() << ','
                    << seq_2x.size() << '\n';
                rows_written++;
            }
        }
    }

    out.close();

    print_stat_line(std::cout, "ROWS_WRITTEN", rows_written);
    print_stat_line(std::cout, "ROWS_SKIPPED", rows_skipped);

    compiler::prog::rotation_manager_end();
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
