/*
 *  author: Claude Code
 *  date:   8 September 2026
 * */

#ifndef COMPILER_PASS_MEMORY_SCHEDULER_MEMORY_STREAM_h
#define COMPILER_PASS_MEMORY_SCHEDULER_MEMORY_STREAM_h

#include "generic_io.h"
#include "globals.h"

#include <bit>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>
#include <vector>

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * This is the on-disk binary "memory stream" format consumed by the
 * standalone Python visualizer in `memory_stream_visualizer/`. The
 * authoritative spec (this file must stay in sync with it) is
 * `memory_stream_visualizer/FORMAT.md`.
 *
 * Layout:  HEADER | BLOCK* | INDEX | FOOTER
 *
 * The footer and index are an optimization, not a requirement: if the
 * process dies mid-run, a reader can recover by scanning forward from
 * the end of the header for block magics, since every block header
 * carries its own `first_epoch_index`/`first_access_index`.
 *
 * All multi-byte fields are native little-endian; we assert this at
 * compile time rather than byte-swap, since every build of this repo
 * targets little-endian hosts.
 * */

static_assert(std::endian::native == std::endian::little,
    "memory_stream.h: on-disk format assumes a little-endian host");

constexpr uint32_t QSMEM_NONE = 0xFFFFFFFFu;

constexpr char QSMEM_HEADER_MAGIC[8] = {'Q','S','M','E','M','S','T','R'};
constexpr char QSMEM_BLOCK_MAGIC[4]  = {'Q','S','M','B',};
constexpr char QSMEM_FOOTER_MAGIC[8] = {'Q','S','M','E','M','E','N','D'};

constexpr uint16_t QSMEM_FORMAT_VERSION = 1;

#pragma pack(push, 1)

struct QsmemHeader
{
    char     magic[8];                  // QSMEM_HEADER_MAGIC
    uint16_t version;                   // QSMEM_FORMAT_VERSION
    uint16_t flags;                     // reserved, currently 0
    uint32_t header_size;               // bytes from start of file to first BLOCK
    uint32_t num_qubits;
    uint32_t active_set_capacity;
    uint32_t block_epoch_capacity;
    uint32_t scheduler_id;
    char     scheduler_name[16];        // NUL-padded, not necessarily NUL-terminated if 16 chars
    // followed by:
    //   uint32_t initial_active_set_len
    //   uint32_t initial_active_set[initial_active_set_len]   (sorted ascending)
};
static_assert(sizeof(QsmemHeader) == 48);
static_assert(std::is_trivially_copyable_v<QsmemHeader>);

struct QsmemBlockHeader
{
    char     magic[4];                  // QSMEM_BLOCK_MAGIC
    uint32_t epoch_count;
    uint64_t first_epoch_index;
    uint64_t access_count;
    uint64_t first_access_index;
    // followed by:
    //   QsmemEpochRecord epochs[epoch_count]
    //   QsmemAccessRecord accesses[access_count]
};
static_assert(sizeof(QsmemBlockHeader) == 32);
static_assert(std::is_trivially_copyable_v<QsmemBlockHeader>);

struct QsmemEpochRecord
{
    uint64_t inst_done;                 // cumulative unrolled instructions retired before this epoch
    uint64_t access_begin;              // global index of this epoch's first AccessRecord
    uint32_t access_count;
    uint32_t compute_since_prev;        // unrolled instructions retired since the previous epoch
    uint32_t unused_bandwidth;
    uint32_t front_layer_width;
    uint32_t dag_inst_count;
    uint32_t reserved{0};
};
static_assert(sizeof(QsmemEpochRecord) == 40);
static_assert(std::is_trivially_copyable_v<QsmemEpochRecord>);

struct QsmemAccessRecord
{
    uint32_t load_qubit;                // QSMEM_NONE if this access has no load side
    uint32_t evict_qubit;               // QSMEM_NONE if this access has no evict side
};
static_assert(sizeof(QsmemAccessRecord) == 8);
static_assert(std::is_trivially_copyable_v<QsmemAccessRecord>);

struct QsmemIndexEntry
{
    uint64_t block_file_offset;
    uint64_t first_epoch_index;
    uint32_t epoch_count;
    uint32_t reserved{0};
    // followed by: uint32_t keyframe[active_set_capacity]  (sorted, QSMEM_NONE padded)
};
static_assert(sizeof(QsmemIndexEntry) == 24);
static_assert(std::is_trivially_copyable_v<QsmemIndexEntry>);

struct QsmemFooter
{
    uint64_t index_offset;
    uint64_t block_count;
    uint64_t total_epochs;
    uint64_t total_accesses;
    char     magic[8];                  // QSMEM_FOOTER_MAGIC
};
static_assert(sizeof(QsmemFooter) == 40);
static_assert(std::is_trivially_copyable_v<QsmemFooter>);

#pragma pack(pop)

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/*
 * Writes a `.qsmem` memory-stream file describing the epoch-by-epoch
 * behavior of a memory scheduler run: which qubits were loaded/evicted
 * each scheduling epoch, and per-epoch metrics (compute since previous
 * epoch, unused bandwidth, front-layer width, DAG size).
 *
 * Usage:
 *   1. Construct once `num_qubits`/`active_set_capacity` are known and
 *      the initial active set has been chosen.
 *   2. For each scheduling epoch: call `record_access` once per
 *      load/evict pair produced by the scheduler, THEN call
 *      `record_epoch` with the resulting active set.
 *   3. `finish()` (or simply destroying the writer) flushes any partial
 *      block and writes the index + footer.
 *
 * `record_access` must be called with data copied out of the
 * `Instruction*` objects BEFORE those instructions are handed to
 * `IOUtility::write_instruction(s)`, which takes ownership and may
 * delete them once its internal buffer drains.
 * */
class MemoryStreamWriter
{
public:
    MemoryStreamWriter(generic_strm_type& strm,
                       uint32_t num_qubits,
                       uint32_t active_set_capacity,
                       uint32_t scheduler_id,
                       std::string_view scheduler_name,
                       const std::unordered_set<qubit_type>& initial_active_set,
                       uint32_t block_epoch_capacity = 4096);
    ~MemoryStreamWriter();

    MemoryStreamWriter(const MemoryStreamWriter&) = delete;
    MemoryStreamWriter& operator=(const MemoryStreamWriter&) = delete;

    /*
     * Records one load/evict pair for the epoch currently being
     * accumulated. Either side may be `-1` (no such qubit) for
     * unpaired LOAD/STORE instructions; both must be valid qubit ids
     * otherwise.
     * */
    void record_access(qubit_type load, qubit_type evict);

    /*
     * Closes out the current epoch: attaches all accesses recorded via
     * `record_access` since the previous call, records the given
     * metrics, and stores `new_active_set` as the post-epoch active
     * set (used to build the next block's keyframe).
     * */
    void record_epoch(uint64_t inst_done,
                       uint32_t compute_since_prev,
                       uint32_t unused_bandwidth,
                       uint32_t front_layer_width,
                       uint32_t dag_inst_count,
                       const std::unordered_set<qubit_type>& new_active_set);

    /*
     * Flushes any partial block and writes the index + footer.
     * Idempotent: calling this more than once (or letting the
     * destructor call it after an explicit call) is a no-op the
     * second time.
     * */
    void finish();

private:
    generic_strm_type& strm_;

    const uint32_t active_set_capacity_;
    const uint32_t block_epoch_capacity_;

    uint64_t bytes_written_{0};

    std::vector<QsmemEpochRecord>  block_epochs_;
    std::vector<QsmemAccessRecord> block_accesses_;
    std::vector<QsmemAccessRecord> pending_accesses_;   // accumulating for the in-flight epoch

    std::vector<QsmemIndexEntry>  index_;
    std::vector<uint32_t>         keyframes_;           // flat, active_set_capacity_ u32s per index entry

    // Sorted+padded active set as of entering the block currently being
    // accumulated. Pushed into `keyframes_` at flush time, then replaced
    // with `latest_active_set_` for the next block.
    std::vector<uint32_t> current_block_keyframe_;

    // Sorted+padded active set as of the most recently recorded epoch.
    // Becomes `current_block_keyframe_` for the next block once this one flushes.
    std::vector<uint32_t> latest_active_set_;

    uint64_t first_epoch_index_of_block_{0};
    uint64_t first_access_index_of_block_{0};

    uint64_t total_epochs_{0};
    uint64_t total_accesses_{0};

    bool finished_{false};

    void write_raw(const void* data, size_t size);
    void flush_block();
    static std::vector<uint32_t> sorted_padded_active_set(const std::unordered_set<qubit_type>&, uint32_t capacity);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler

#endif  // COMPILER_PASS_MEMORY_SCHEDULER_MEMORY_STREAM_h
