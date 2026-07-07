/*
 *  author: Claude (Anthropic)
 *
 *  Validation tests for the physical-qubit-count formulas in
 *  `sim/configuration/resource_estimation.{h,tpp}`.
 *
 *  These are pure `constexpr` functions, so a known input maps to a known
 *  output with no state involved.
 * */

#include "sim/configuration/resource_estimation.h"
#include "test_util.h"

#include <cstdio>

using namespace sim::configuration;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Surface code: `surface_code_physical_qubit_count(dx, dz) == 2*(dx+1)*(dz+1)`.
 * (The single-argument overload is `(d, d)`.)
 * */
void
test_surface_code_physical_qubit_count()
{
    // proof that these evaluate at compile time
    static_assert(surface_code_physical_qubit_count(3) == 32);
    static_assert(surface_code_physical_qubit_count(3, 5) == 48);

    // single-argument (square) form: (d, d)
    CHECK_EQ(surface_code_physical_qubit_count(1),  size_t{8});    // 2*2*2
    CHECK_EQ(surface_code_physical_qubit_count(3),  size_t{32});   // 2*4*4
    CHECK_EQ(surface_code_physical_qubit_count(5),  size_t{72});   // 2*6*6
    CHECK_EQ(surface_code_physical_qubit_count(7),  size_t{128});  // 2*8*8
    CHECK_EQ(surface_code_physical_qubit_count(11), size_t{288});  // 2*12*12

    // single-arg matches the (d, d) overload
    CHECK_EQ(surface_code_physical_qubit_count(4), surface_code_physical_qubit_count(4, 4));

    // asymmetric (dx, dz) combinations, and symmetry under swapping dx/dz
    CHECK_EQ(surface_code_physical_qubit_count(3, 5), size_t{48});   // 2*4*6
    CHECK_EQ(surface_code_physical_qubit_count(5, 3), size_t{48});   // 2*6*4
    CHECK_EQ(surface_code_physical_qubit_count(7, 9), size_t{160});  // 2*8*10
    CHECK_EQ(surface_code_physical_qubit_count(9, 7), size_t{160});  // 2*10*8
    CHECK_EQ(surface_code_physical_qubit_count(2, 4), size_t{30});   // 2*3*5
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Bivariate-bicycle code: for the [[n, k, d]] family
 *   d=6  -> [[72, 12, 6]]
 *   d=12 -> [[144, 12, 12]]
 *   d=18 -> [[288, 12, 18]]
 * the physical-qubit count is `2n` plus a fixed routing-adapter term of
 * `50 << (d/6 - 1)`. The counts below are the full value (2n + adapter):
 *   2*72  + 50   = 194
 *   2*144 + 100  = 388
 *   2*288 + 200  = 776
 * */
void
test_bivariate_bicycle_physical_qubit_count()
{
    static_assert(bivariate_bicycle_code_physical_qubit_count(6) == 194);

    CHECK_EQ(bivariate_bicycle_code_physical_qubit_count(6),  size_t{194});  // 2*72  + 50
    CHECK_EQ(bivariate_bicycle_code_physical_qubit_count(12), size_t{388});  // 2*144 + 100
    CHECK_EQ(bivariate_bicycle_code_physical_qubit_count(18), size_t{776});  // 2*288 + 200
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Reference footprint from Litinski, "Magic State Distillation: Not as Costly
 * as You Think" (Quantum 3, 2019), Sec. 3 / Fig. 11: the single-level 15-to-1
 * protocol occupies
 *
 *     N = 2*(dx + 4*dz)*3*dx + 4*dm   physical qubits   (for 6*dm code cycles).
 *
 * This reproduces the paper's Table 1 entry (15-to-1)_{7,3,3} = 810 exactly.
 * (Taller configs differ from Table 1 by <=4 qubits, a measurement-ancilla
 * rounding detail; e.g. (9,3,3): formula 1146 vs table 1150.)
 * */
constexpr size_t
litinski_15to1_physical_qubit_count(size_t dx, size_t dz, size_t dm)
{
    return 2*(dx + 4*dz)*3*dx + 4*dm;
}

/*
 * Compares the codebase footprint estimate,
 *   magic_state_distillation_physical_qubit_count(input_count, output_count, dx, dz),
 * against Litinski's single-level 15-to-1 formula for a range of (dx, dz, dm).
 *
 * The two models are structurally different and are NOT expected to match:
 *  - Litinski depends linearly on dx, dz, dm.
 *  - The codebase estimate depends on input/output counts and surface-patch
 *    sizes 2*(d+1)^2, and takes no dm (so varying dm alone moves only the
 *    Litinski column).
 * This test reports the delta so the divergence is visible; it asserts only
 * the two paper/anchor values and basic sanity.
 * */
void
test_distillation_footprint_delta_vs_litinski()
{
    constexpr size_t INPUT_COUNT{15};   // "15-to-1": 15 inputs ...
    constexpr size_t OUTPUT_COUNT{1};   // ... 1 output

    // anchor 1: Litinski formula matches the paper's smallest 15-to-1 footprint
    CHECK_EQ(litinski_15to1_physical_qubit_count(7, 3, 3), size_t{810});
    // anchor 2: pin the codebase estimate (also guards the input_pq_count fix)
    CHECK_EQ(magic_state_distillation_physical_qubit_count(INPUT_COUNT, OUTPUT_COUNT, 7, 3), size_t{1056});

    struct { size_t dx, dz, dm; } cases[] = {
        {7, 3, 3}, {9, 3, 3}, {11, 5, 5}, {15, 7, 7}, {25, 11, 11},
    };

    std::printf("  dx  dz  dm | litinski  codebase |   delta\n");
    std::printf("  --------------------------------------------\n");
    for (auto c : cases)
    {
        const size_t litinski = litinski_15to1_physical_qubit_count(c.dx, c.dz, c.dm);
        const size_t codebase = magic_state_distillation_physical_qubit_count(INPUT_COUNT, OUTPUT_COUNT, c.dx, c.dz);
        const long   delta    = static_cast<long>(codebase) - static_cast<long>(litinski);
        std::printf("  %2zu  %2zu  %2zu | %8zu  %8zu | %+7ld\n",
                    c.dx, c.dz, c.dm, litinski, codebase, delta);

        CHECK(litinski > 0);
        CHECK(codebase > 0);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main()
{
    RUN(test_surface_code_physical_qubit_count);
    RUN(test_bivariate_bicycle_physical_qubit_count);
    RUN(test_distillation_footprint_delta_vs_litinski);
    return qstest::finish();
}
