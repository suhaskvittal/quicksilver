/*
 *  author: Claude (Anthropic)
 *
 *  Validation tests for `MemoryLevel::striped_mapping` in `sim/memory_level.h`.
 *
 *  `striped_mapping` round-robins the qubits in the range `[begin, end)` across
 *  the level's `num_blocks` code blocks: qubit at position `i` is inserted into
 *  block `i % num_blocks`. Block membership is by pointer identity.
 *
 *  `MemoryLevel` is abstract, so we exercise it through the concrete `BBMemory`
 *  subclass and inspect placement via `MemoryLevel::blocks()`.
 * */

#include "sim/memory/bivariate_bicycle.h"
#include "sim/qubit.h"
#include "test_util.h"

#include <algorithm>
#include <limits>
#include <vector>

using namespace sim;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

// BB code parameters for a [[72, 12, 6]] block: n = 72, k = 12, d = 6.
constexpr size_t BB_N{72};
constexpr size_t BB_K{12};
constexpr size_t BB_D{6};

/*
 * Assigns distinct ids and returns pointers into `v` (which must outlive the
 * mapping, so its addresses stay stable).
 * */
std::vector<Qubit*>
pointers_to(std::vector<Qubit>& v)
{
    std::vector<Qubit*> ptrs;
    ptrs.reserve(v.size());
    for (size_t i = 0; i < v.size(); i++)
    {
        v[i].qubit_id = static_cast<qubit_type>(i);
        ptrs.push_back(&v[i]);
    }
    return ptrs;
}

}  // anonymous namespace

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * The constructor sets `num_blocks = ceil(qubit_count / k)` and rounds the total
 * capacity up to `num_blocks * k`.
 * */
void
test_block_count_and_capacity()
{
    // exact multiple: 48 / 12 -> 4 blocks, capacity 48
    BBMemory exact(1000.0, /*qubit_count=*/48, BB_N, BB_K, BB_D);
    CHECK_EQ(exact.num_blocks,      size_t{4});
    CHECK_EQ(exact.total_capacity,  size_t{48});
    CHECK_EQ(exact.blocks().size(), size_t{4});

    // non-multiple rounds up: ceil(40 / 12) = 4 blocks, capacity 4*12 = 48
    BBMemory rounded(1000.0, /*qubit_count=*/40, BB_N, BB_K, BB_D);
    CHECK_EQ(rounded.num_blocks,     size_t{4});
    CHECK_EQ(rounded.total_capacity, size_t{48});
}

/*
 * Round-robin with wrap-around: 10 qubits over 4 blocks. Qubit `i` lands in
 * block `i % 4`, giving block sizes {3, 3, 2, 2}.
 * */
void
test_striped_mapping_round_robin()
{
    BBMemory mem(1000.0, 48, BB_N, BB_K, BB_D);   // num_blocks = 4
    const size_t nb = mem.num_blocks;

    std::vector<Qubit> qubits(10);
    auto ptrs = pointers_to(qubits);
    mem.striped_mapping(ptrs.begin(), ptrs.end());

    const auto& blocks = mem.blocks();

    // exact placement: qubit i is in block i % num_blocks (and nowhere else)
    for (size_t i = 0; i < qubits.size(); i++)
    {
        CHECK_EQ(blocks[i % nb].count(&qubits[i]), size_t{1});
        for (size_t b = 0; b < nb; b++)
            if (b != i % nb)
                CHECK_EQ(blocks[b].count(&qubits[i]), size_t{0});
    }

    // all qubits placed, and the distribution is balanced (sizes differ by <= 1)
    size_t total = 0;
    size_t mn = std::numeric_limits<size_t>::max();
    size_t mx = 0;
    for (const auto& b : blocks)
    {
        total += b.size();
        mn = std::min(mn, b.size());
        mx = std::max(mx, b.size());
    }
    CHECK_EQ(total, size_t{10});
    CHECK(mx - mn <= 1);
    CHECK_EQ(mx, size_t{3});
    CHECK_EQ(mn, size_t{2});
}

/*
 * Fewer qubits than blocks: 3 qubits over 4 blocks fills blocks 0-2 and leaves
 * block 3 empty.
 * */
void
test_striped_mapping_fewer_than_blocks()
{
    BBMemory mem(1000.0, 48, BB_N, BB_K, BB_D);   // num_blocks = 4

    std::vector<Qubit> qubits(3);
    auto ptrs = pointers_to(qubits);
    mem.striped_mapping(ptrs.begin(), ptrs.end());

    const auto& blocks = mem.blocks();
    for (size_t i = 0; i < qubits.size(); i++)
        CHECK_EQ(blocks[i].count(&qubits[i]), size_t{1});
    CHECK_EQ(blocks[3].size(), size_t{0});
}

/*
 * Exact multiple of the block count: 8 qubits over 4 blocks gives 2 per block.
 * */
void
test_striped_mapping_exact_multiple()
{
    BBMemory mem(1000.0, 48, BB_N, BB_K, BB_D);   // num_blocks = 4
    const size_t nb = mem.num_blocks;

    std::vector<Qubit> qubits(8);
    auto ptrs = pointers_to(qubits);
    mem.striped_mapping(ptrs.begin(), ptrs.end());

    const auto& blocks = mem.blocks();
    for (const auto& b : blocks)
        CHECK_EQ(b.size(), size_t{2});
    for (size_t i = 0; i < qubits.size(); i++)
        CHECK_EQ(blocks[i % nb].count(&qubits[i]), size_t{1});
}

/*
 * Empty range: nothing is inserted; all blocks stay empty.
 * */
void
test_striped_mapping_empty()
{
    BBMemory mem(1000.0, 48, BB_N, BB_K, BB_D);

    std::vector<Qubit*> none;
    mem.striped_mapping(none.begin(), none.end());

    for (const auto& b : mem.blocks())
        CHECK_EQ(b.size(), size_t{0});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main()
{
    RUN(test_block_count_and_capacity);
    RUN(test_striped_mapping_round_robin);
    RUN(test_striped_mapping_fewer_than_blocks);
    RUN(test_striped_mapping_exact_multiple);
    RUN(test_striped_mapping_empty);
    return qstest::finish();
}
