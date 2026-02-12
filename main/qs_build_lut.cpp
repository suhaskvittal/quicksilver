/*
 *  author: Suhas Vittal
 *  date:   11 February 2026
 * */

#include "argparse.h"
#include "compiler/program/rotation_synthesis.h"
#include "fixed_point/angle.h"
#include "generic_io.h"
#include "instruction.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using urotseq_type = INSTRUCTION::urotseq_type;

/*
 * Writes a single LUT entry to the stream in the binary format
 * expected by `_read_lut_from_file`:
 *   1B  word count
 *   N*8B FPA words
 *   2B  sequence length
 *   seq_len*1B gate bytes
 * */
void _write_entry(generic_strm_type&, double, const urotseq_type&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    double      lo, hi;
    int64_t     count;
    std::string output_file;
    int64_t     num_threads;

    ARGPARSE()
        .required("lower-bound", "lower bound of angle range", lo)
        .required("upper-bound", "upper bound of angle range", hi)
        .required("count", "number of angles to generate", count)
        .required("output", "output file path", output_file)
        .optional("-t", "--threads", "number of worker threads", num_threads, int64_t{8})
        .parse(argc, argv);

    if (lo < 0.0 && hi > 0.0)
        std::cerr << "qs_build_lut: angle range must not cross zero" << _die{};

    // convert negatives into positives for simplicity
    const bool angles_are_negative = lo < 0.0 && hi < 0.0;
    lo = std::abs(lo);
    hi = std::abs(hi);

    // Collect angles in increasing |angle| order so the output file satisfies
    // `_read_lut_from_file`'s sorted-entry assertion.
    std::vector<double> angles(count);
    const double step = (hi-lo) / static_cast<double>(count);
    for (size_t i = 0; i < count; i++)
    {
        angles[i] = lo + step*i;
        if (angles_are_negative)
            angles[i] = -angles[i];
    }

    generic_strm_type ostrm;
    generic_strm_open(ostrm, output_file, "wb");

    size_t batch_start{0};
    std::cout << "progress:\t";
    while (batch_start < angles.size())
    {
        (std::cout << ".").flush();

        const size_t batch_end  = std::min(batch_start + num_threads, angles.size());
        const size_t batch_size = batch_end - batch_start;

        std::vector<urotseq_type> results(batch_size);
        std::vector<std::thread>  threads(batch_size);

        for (size_t i = 0; i < batch_size; i++)
        {
            size_t idx = batch_start + i;
            double a = angles[idx];
            ssize_t p = static_cast<ssize_t>( std::ceil(-std::log10(std::abs(a))) + 5 );

            threads[i] = std::thread([&results, a, i, p] () 
                            { 
                                auto fpa = convert_float_to_fpa<64>(a, std::pow(10, -p));
                                results[i] = prog::synthesize_rotation(fpa, p, false);
//                              prog::validate_urotseq(results[i], fpa, p);
                            });
        }

        for (auto& t : threads)
            t.join();

        for (size_t i = 0; i < batch_size; i++)
            _write_entry(ostrm, angles[batch_start + i], results[i]);

        batch_start = batch_end;
    }

    generic_strm_close(ostrm);
    std::cout << "\nqs_build_lut: done\n";
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTION DEFINITIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_write_entry(generic_strm_type& strm, double angle, const urotseq_type& urotseq)
{
    uint16_t seq_len = static_cast<uint16_t>(urotseq.size());

    generic_strm_write(strm, &angle, sizeof(angle));
    generic_strm_write(strm, &seq_len, sizeof(seq_len));
    for (auto gate : urotseq)
    {
        uint8_t g = static_cast<uint8_t>(gate);
        generic_strm_write(strm, &g, sizeof(g));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
