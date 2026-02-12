/*
    author: Suhas Vittal
    date:   06 October 2025
*/

#include "compiler/program/rotation_manager.h"
#include "generic_io.h"

namespace prog
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using fpa_type = INSTRUCTION::fpa_type;
using urotseq_type = INSTRUCTION::urotseq_type;

constexpr size_t LUT_COUNT{10*2};  // 12 buckets, one positive and one negative

struct lut_entry
{
    double       angle{};
    urotseq_type urotseq;
};

using lut_type = std::vector<lut_entry>;
using lut_array = std::array<lut_type, LUT_COUNT>;

/*
 * Static variables:
 * */

static lut_array LUT{};

/*
 * Helper functions:
 * */

/*
 * Reads the rotation data from the file and returns a vector (sorted by floating point value).
 * */
lut_type _read_lut_from_file(generic_strm_type&);

/*
 * Performs a binary search and returns the closest LUT entry (in terms of value) to the given angle.
 * */
const lut_entry& _search_for_nearest_entry_in_lut(const lut_type&, const fpa_type&);

/*
 * Returns the LUT index for the given angle.
 * */
size_t _get_lut_array_idx(const fpa_type&);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rotation_manager_init()
{
#if !defined(ROTATION_SYNTHESIS_LUT_FOLDER_PATH)
    std::cerr << "rotation_manager_init: ROTATION_SYNTHESIS_LUT_FOLDER_PATH not defined by build system"
            << _die{};
#endif

    generic_strm_type istrm;
    for (size_t i = 0; i < LUT_COUNT; i++)
    {
        std::string file_path = std::string{ROTATION_SYNTHESIS_LUT_FOLDER_PATH} + "/" + std::to_string(i) + ".bin.xz";
        generic_strm_open(istrm, file_path, "rb");
        LUT[i] = _read_lut_from_file(istrm);
        generic_strm_close(istrm);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
rotation_manager_end()
{
    for (auto& x : LUT)
        x.clear();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

urotseq_type
rotation_manager_lookup(const fpa_type& angle)
{
    if (angle.popcount() == 0)
        return {};

    size_t idx = _get_lut_array_idx(angle);
    const auto& e = _search_for_nearest_entry_in_lut(LUT[idx], angle);

    double f = std::abs(convert_fpa_to_float(angle));
    double delta = std::abs(std::abs(e.angle) - f);
    if (delta > 1e-2*f)
    {
        std::cerr << "rotation_manager_lookup: did not find angle within acceptable tolerance."
                    << " got: " << e.angle << ", wanted: " << f << ", tol = " << (1e-2*f) << ", delta = " << delta
                    << _die{};
    }

    return e.urotseq;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* HELPER FUNCTIONS START HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

lut_type
_read_lut_from_file(generic_strm_type& strm)
{
    constexpr size_t UROTSEQ_CAPACITY{256};

    lut_type out;
    out.reserve(1024);

    /*
     * format:
     *  - 8B floating point angle
     *  - 2B urotseq byte count
     *  - urotseq data
     * */
    double   angle;
    uint16_t urotseq_byte_count;
    uint8_t  urotseq_bytes[UROTSEQ_CAPACITY];

    while (!generic_strm_eof(strm))
    {
        generic_strm_read(strm, &angle, sizeof(angle));

        generic_strm_read(strm, &urotseq_byte_count, sizeof(urotseq_byte_count));
        assert(urotseq_byte_count <= UROTSEQ_CAPACITY);

        generic_strm_read(strm, urotseq_bytes, sizeof(uint8_t)*urotseq_byte_count);

        // use data to build `lut_entry`
        urotseq_type urotseq(urotseq_byte_count);
        std::transform(urotseq_bytes, urotseq_bytes+urotseq_byte_count, urotseq.begin(), 
                        [] (uint8_t b) { return static_cast<INSTRUCTION::TYPE>(b); });

        lut_entry e{angle, urotseq};
        // assert that `out` remains sorted if we add `e`
        assert(out.empty() || std::abs(out.back().angle) < std::abs(e.angle));
        out.push_back(e);
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const lut_entry&
_search_for_nearest_entry_in_lut(const lut_type& lut, const fpa_type& angle)
{
    const double f = std::abs(convert_fpa_to_float(angle));
    const double eps = 1e-2*f;

    int left{0},
        right{lut.size()-1};
    while (left < right)
    {
        const auto& a = lut.at(left),
                  & b = lut.at(right);
        const double a_fp = std::abs(a.angle),
                     b_fp = std::abs(b.angle);
        
        if (std::abs(f-a_fp) < eps || right-left == 1)
            return a;
        else if (std::abs(f-b_fp) < eps || right-left == 1)
            return b;

        int mid = (left+right) >> 1;
        const auto& m = lut.at(mid);
        if (f < std::abs(m.angle))
            right = mid;
        else
            left = mid;
    }

    std::cerr << "_search_for_nearest_entry_in_lut: could not find match for angle: " 
            << fpa::to_string(angle) << ", fp = " << f
            << "\tmin of LUT = " << lut.front().angle << ", max of LUT = " << lut.back().angle
            << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_get_lut_array_idx(const fpa_type& angle)
{
    double f = convert_fpa_to_float(angle);
    bool is_negative = (f < 0.0);
    if (is_negative)
        f = -f;

    int idx{0};
    while (idx < LUT_COUNT/2)
    {
        double lower_bound = std::pow(10.0, -idx),
               upper_bound = (idx == 0) ? 2*M_PI : std::pow(10.0, -idx+1);
        if (f > lower_bound && f < upper_bound)
            return is_negative ? idx+(LUT_COUNT/2) : idx;
        else
            idx++;
    }

    std::cerr << "_get_lut_array_idx: could not find LUT angle: " 
            << fpa::to_string(angle) << ", fp = " << f
            << _die{};
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace prog
