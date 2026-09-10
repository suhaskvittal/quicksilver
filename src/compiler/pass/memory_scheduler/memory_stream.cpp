/*
 *  author: Claude Code
 *  date:   8 September 2026
 * */

#include "compiler/pass/memory_scheduler/memory_stream.h"

#include <algorithm>
#include <cstring>
#include <variant>

namespace compiler
{
namespace pass
{
namespace memory_scheduler
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MemoryStreamWriter::MemoryStreamWriter(generic_strm_type& strm,
                                        uint32_t num_qubits,
                                        uint32_t active_set_capacity,
                                        uint32_t scheduler_id,
                                        std::string_view scheduler_name,
                                        const std::unordered_set<qubit_type>& initial_active_set,
                                        uint32_t block_epoch_capacity)
    :strm_(strm),
     active_set_capacity_(active_set_capacity),
     block_epoch_capacity_(block_epoch_capacity)
{
    if (initial_active_set.size() > active_set_capacity_)
    {
        std::cerr << "MemoryStreamWriter: initial active set (" << initial_active_set.size()
                    << ") exceeds active_set_capacity (" << active_set_capacity_ << ")" << _die{};
    }

    QsmemHeader header{};
    std::memcpy(header.magic, QSMEM_HEADER_MAGIC, sizeof(header.magic));
    header.version = QSMEM_FORMAT_VERSION;
    header.flags = 0;
    header.num_qubits = num_qubits;
    header.active_set_capacity = active_set_capacity_;
    header.block_epoch_capacity = block_epoch_capacity_;
    header.scheduler_id = scheduler_id;

    std::memset(header.scheduler_name, 0, sizeof(header.scheduler_name));
    std::memcpy(header.scheduler_name, scheduler_name.data(),
                std::min(scheduler_name.size(), sizeof(header.scheduler_name)));

    // sorted, NOT padded -- this is a length-prefixed list, not a fixed-stride keyframe
    std::vector<uint32_t> sorted_initial;
    sorted_initial.reserve(initial_active_set.size());
    for (qubit_type q : initial_active_set)
    {
        if (q < 0 || static_cast<uint64_t>(q) >= QSMEM_NONE)
            std::cerr << "MemoryStreamWriter: qubit id " << q << " out of representable range" << _die{};
        sorted_initial.push_back(static_cast<uint32_t>(q));
    }
    std::sort(sorted_initial.begin(), sorted_initial.end());

    header.header_size = static_cast<uint32_t>(sizeof(QsmemHeader) + sizeof(uint32_t)
                                                + sorted_initial.size() * sizeof(uint32_t));

    write_raw(&header, sizeof(header));
    uint32_t n = static_cast<uint32_t>(sorted_initial.size());
    write_raw(&n, sizeof(n));
    if (!sorted_initial.empty())
        write_raw(sorted_initial.data(), sorted_initial.size() * sizeof(uint32_t));

    current_block_keyframe_ = sorted_padded_active_set(initial_active_set, active_set_capacity_);
    latest_active_set_      = current_block_keyframe_;
}

MemoryStreamWriter::~MemoryStreamWriter()
{
    finish();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MemoryStreamWriter::write_raw(const void* data, size_t size)
{
    generic_strm_write(strm_, const_cast<void*>(data), size);
    bytes_written_ += size;
}

/*
 * Flushes the underlying OS-level stream so that, if the process dies
 * right after this call, at most the block currently being accumulated
 * (not yet flushed) is lost -- everything written so far is durable.
 * Plain FILE or gzFile only: `strm_` is never an LZMAFile here (the driver
 * rejects `.xz` output paths before construction, since LZMAFile is
 * decoder-only).
 * */
static void
flush_strm(generic_strm_type& strm)
{
    if (std::holds_alternative<FILE*>(strm))
        std::fflush(std::get<FILE*>(strm));
    else if (std::holds_alternative<gzFile>(strm))
        gzflush(std::get<gzFile>(strm), Z_SYNC_FLUSH);
}

std::vector<uint32_t>
MemoryStreamWriter::sorted_padded_active_set(const std::unordered_set<qubit_type>& s, uint32_t capacity)
{
    if (s.size() > capacity)
        std::cerr << "MemoryStreamWriter: active set of size " << s.size()
                    << " exceeds capacity " << capacity << _die{};

    std::vector<uint32_t> out;
    out.reserve(capacity);
    for (qubit_type q : s)
    {
        if (q < 0 || static_cast<uint64_t>(q) >= QSMEM_NONE)
            std::cerr << "MemoryStreamWriter: qubit id " << q << " out of representable range" << _die{};
        out.push_back(static_cast<uint32_t>(q));
    }
    std::sort(out.begin(), out.end());
    out.resize(capacity, QSMEM_NONE);
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MemoryStreamWriter::record_access(qubit_type load, qubit_type evict)
{
    auto to_field = [] (qubit_type q) -> uint32_t
    {
        if (q < 0)
            return QSMEM_NONE;
        if (static_cast<uint64_t>(q) >= QSMEM_NONE)
            std::cerr << "MemoryStreamWriter: qubit id " << q << " out of representable range" << _die{};
        return static_cast<uint32_t>(q);
    };

    QsmemAccessRecord rec{};
    rec.load_qubit  = to_field(load);
    rec.evict_qubit = to_field(evict);
    pending_accesses_.push_back(rec);
}

void
MemoryStreamWriter::record_epoch(uint64_t inst_done,
                                  uint32_t compute_since_prev,
                                  uint32_t unused_bandwidth,
                                  uint32_t front_layer_width,
                                  uint32_t dag_inst_count,
                                  const std::unordered_set<qubit_type>& new_active_set)
{
    QsmemEpochRecord rec{};
    rec.inst_done           = inst_done;
    rec.access_begin        = first_access_index_of_block_ + block_accesses_.size();
    rec.access_count        = static_cast<uint32_t>(pending_accesses_.size());
    rec.compute_since_prev  = compute_since_prev;
    rec.unused_bandwidth    = unused_bandwidth;
    rec.front_layer_width   = front_layer_width;
    rec.dag_inst_count      = dag_inst_count;

    block_epochs_.push_back(rec);
    block_accesses_.insert(block_accesses_.end(), pending_accesses_.begin(), pending_accesses_.end());

    total_epochs_++;
    total_accesses_ += pending_accesses_.size();
    pending_accesses_.clear();

    // Track the active set as of the most recent epoch. This becomes the
    // NEXT block's keyframe once the current block flushes (see flush_block).
    latest_active_set_ = sorted_padded_active_set(new_active_set, active_set_capacity_);

    if (block_epochs_.size() >= block_epoch_capacity_)
        flush_block();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MemoryStreamWriter::flush_block()
{
    if (block_epochs_.empty())
        return;

    QsmemIndexEntry idx{};
    idx.block_file_offset = bytes_written_;
    idx.first_epoch_index = first_epoch_index_of_block_;
    idx.epoch_count        = static_cast<uint32_t>(block_epochs_.size());
    index_.push_back(idx);

    // `current_block_keyframe_` is the active set as it existed ENTERING this
    // block (i.e. before any of the epochs we're about to write ran).
    keyframes_.insert(keyframes_.end(),
                       current_block_keyframe_.begin(),
                       current_block_keyframe_.end());

    QsmemBlockHeader hdr{};
    std::memcpy(hdr.magic, QSMEM_BLOCK_MAGIC, sizeof(hdr.magic));
    hdr.epoch_count        = static_cast<uint32_t>(block_epochs_.size());
    hdr.first_epoch_index  = first_epoch_index_of_block_;
    hdr.access_count       = block_accesses_.size();
    hdr.first_access_index = first_access_index_of_block_;

    write_raw(&hdr, sizeof(hdr));
    write_raw(block_epochs_.data(), block_epochs_.size() * sizeof(QsmemEpochRecord));
    if (!block_accesses_.empty())
        write_raw(block_accesses_.data(), block_accesses_.size() * sizeof(QsmemAccessRecord));

    first_epoch_index_of_block_  += block_epochs_.size();
    first_access_index_of_block_ += block_accesses_.size();

    block_epochs_.clear();
    block_accesses_.clear();

    // The next block is entered with whatever active set resulted from the
    // last epoch we just flushed.
    current_block_keyframe_ = latest_active_set_;

    flush_strm(strm_);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MemoryStreamWriter::finish()
{
    if (finished_)
        return;
    finished_ = true;

    flush_block();

    QsmemFooter footer{};
    footer.index_offset   = bytes_written_;
    footer.block_count    = index_.size();
    footer.total_epochs   = total_epochs_;
    footer.total_accesses = total_accesses_;
    std::memcpy(footer.magic, QSMEM_FOOTER_MAGIC, sizeof(footer.magic));

    for (size_t i = 0; i < index_.size(); i++)
    {
        write_raw(&index_[i], sizeof(QsmemIndexEntry));
        const uint32_t* kf = &keyframes_[i * active_set_capacity_];
        write_raw(kf, active_set_capacity_ * sizeof(uint32_t));
    }

    write_raw(&footer, sizeof(footer));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace memory_scheduler
}  // namespace pass
}  // namespace compiler
