/*
 *  author: Suhas Vittal
 *  date:   21 January 2026
 * */

#include "sim/compute_subsystem.h"
#include "sim/rotation_subsystem.h"

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

using inst_ptr = ROTATION_SUBSYSTEM::inst_ptr;
using rotation_request_entry = ROTATION_SUBSYSTEM::rotation_request_entry;

bool _request_compare(const rotation_request_entry*, const rotation_request_entry*);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::ROTATION_SUBSYSTEM(double freq_khz,
                                        size_t code_distance,
                                        size_t capacity,
                                        COMPUTE_SUBSYSTEM* parent,
                                        double watermark)
    :COMPUTE_BASE("rotation_subsystem", 
                    freq_khz, 
                    code_distance,
                    capacity, 
                    parent->top_level_t_factories(), 
                    parent->memory_hierarchy()),
    watermark_(watermark),
    parent_(parent)
{
    request_map_.reserve(32);
    free_qubits_.reserve(capacity);

    for (qubit_type i = 0; i < capacity; i++)
    {
        QUBIT* q = new QUBIT{i,-1};
        local_memory_->insert(q);
        free_qubits_.push_back(q);
    }
}

ROTATION_SUBSYSTEM::~ROTATION_SUBSYSTEM()
{
    std::unordered_set<rotation_request_entry*> to_delete;

    for (auto& [inst, req] : request_map_)
        to_delete.insert(req);

    while (!pending_queue_.empty())
    {
        to_delete.insert(pending_queue_.top());
        pending_queue_.pop();
    }

    for (auto* req : to_delete)
        delete req;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::request_priority_comparator::operator()(const rotation_request_entry* a,
                                                            const rotation_request_entry* b) const
{
    return _request_compare(b,a);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_SUBSYSTEM::print_progress(std::ostream& out) const
{
    out << "rotation_subsystem-------------------------"
        << "\n\ttotal pending requests: " << pending_queue_.size()
        << "\n\tpending requests with allocated qubit:";
    for (const auto& [inst, req] : request_map_)
    {
        if (req->allocated_qubit != nullptr)
        {
            out << "\n\t\t" << *inst 
                << ", qubit: " << *req->allocated_qubit
                << ", progress = " << inst->uops_retired()
                << ", done = " << req->done;
        }
    }
    if (active_qubit_ == nullptr)
        out << "\n\tno active qubit\n";
    else
        out << "\n\tactive_qubit = " << *active_qubit_ << "\n";
}

void
ROTATION_SUBSYSTEM::print_deadlock_info(std::ostream& out) const
{
    out << "rotation_subsystem"
        << "\n\tpending requests:";
    for (const auto& p : request_map_)
    {
        std::string allocated_qubit_str = (p.second->allocated_qubit == nullptr)
                                            ? "N/A"
                                            : p.second->allocated_qubit->to_string();

        out << "\n\t\t" << *p.first 
            << " : { .allocated_qubit = " << allocated_qubit_str
            << ", .invalidated = " << p.second->invalidated
            << ", .done = " << p.second->done
            << ", .cycle_installed = " << p.second->cycle_installed
            << ", .cycle_done = " << p.second->cycle_done
            << " }";
    }
    out << "\n\tpending_queue size = " << pending_queue_.size();
    if (active_qubit_ != nullptr)
        out << "\n\tactive qubit = " << *active_qubit_;
    else
        out << "\n\tactive qubit = nullptr";
    out << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::can_accept_request() const
{
//  return !free_qubits_.empty();
    return true;
}

bool
ROTATION_SUBSYSTEM::submit_request(inst_ptr inst, size_t layer, inst_ptr triggering_inst)
{
    if (is_request_pending(inst))
        return false;

    rotation_request_entry* req = new rotation_request_entry
                                    {
                                        .inst=inst,
                                        .dag_layer=layer,
                                        .triggering_inst_info=triggering_inst->to_string()
                                    };

    if (!free_qubits_.empty())
    {
        QUBIT* q = free_qubits_.back();
        free_qubits_.pop_back();
        req->allocated_qubit = q;
        if (active_qubit_ == nullptr)
            active_qubit_ = q;
    }
    else
    {
        pending_queue_.push(req);
    }

    request_map_[inst] = req;
    return true;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::is_request_pending(inst_ptr inst) const
{
    return request_map_.find(inst) != request_map_.end();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::find_and_delete_request_if_done(inst_ptr inst)
{
    auto it = request_map_.find(inst);
    if (it != request_map_.end() && it->second->done)
    {
        auto* req = it->second;

        // update stats:
        cycle_type idle_cycles = parent_->current_cycle() - req->cycle_done;
        s_rotation_service_cycles += req->cycle_done - req->cycle_installed;
        s_rotation_idle_cycles += idle_cycles;
        s_rotations_completed++;

        request_map_.erase(it);
        delete_request(req);
        return true;
    }
    else
    {
        return false;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
ROTATION_SUBSYSTEM::get_progress(inst_ptr inst) const
{
    return inst->uops_retired();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_SUBSYSTEM::mark_critical(inst_ptr inst)
{
    if (!is_request_pending(inst))
        return;
    auto* req = request_map_.at(inst);
    bool was_noncritical_before = req->critical;
    req->critical = true;

    // try to take away the active qubit if possible
    if (was_noncritical_before && req->allocated_qubit != nullptr && active_qubit_ != req->allocated_qubit)
        get_new_active_qubit();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_SUBSYSTEM::invalidate(inst_ptr inst)
{
    auto it = request_map_.find(inst);
    if (it == request_map_.end())
        return;

    auto* req = it->second;
    req->invalidated = true;
    request_map_.erase(it);
    s_invalidates++;

    // If has qubit, delete and hand off. If in pending_queue_,
    // it will be skipped/deleted when popped via pop_next_valid_pending_request().
    if (req->allocated_qubit != nullptr)
        delete_request(req);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

long
ROTATION_SUBSYSTEM::operate()
{
    if (active_qubit_ == nullptr)
        return 1;

    long progress{0};
    auto req_it = std::find_if(request_map_.begin(), request_map_.end(), 
                            [this] (const auto& p) { return p.second->allocated_qubit == this->active_qubit_; });
    assert(req_it != request_map_.end());
    auto* inst = req_it->first;
    auto* req = req_it->second;
    auto* q = req->allocated_qubit;
    assert(!req->done);

    if (q->cycle_available > current_cycle())
        return 0;

    req->cycle_installed = std::min(parent_->current_cycle(), req->cycle_installed);

    const size_t num_teleports = (req->critical || GL_RPC_ALWAYS_USE_TELEPORTATION) 
                                    ? GL_T_GATE_TELEPORTATION_MAX
                                    : 0;

    auto result = do_rotation_gate_with_teleportation_while_predicate_holds(inst, {q}, num_teleports,
                    [this, req] (const inst_ptr x, const inst_ptr uop)
                    {
                        const size_t m = count_available_magic_states();
                        const size_t min_t_count = 1;
                        return req->critical || (m > min_t_count);
                    });
    progress += result.progress;
    if (result.progress > 0 && inst->uops_retired() == inst->uop_count())
    {
        // instruction is done -- reset uop progress for safety
        inst->reset_uops();
        req->done = true;
        req->cycle_done = parent_->current_cycle();

        get_new_active_qubit();
    }

    return progress;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_SUBSYSTEM::delete_request(rotation_request_entry* req)
{
    QUBIT* freed_qubit = req->allocated_qubit;

    rotation_request_entry* next_req = pop_next_valid_pending_request();
    if (next_req != nullptr)
        next_req->allocated_qubit = freed_qubit;
    else
        free_qubits_.push_back(freed_qubit);

    if (freed_qubit == active_qubit_ || active_qubit_ == nullptr)
        get_new_active_qubit();

    delete req;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::rotation_request_entry*
ROTATION_SUBSYSTEM::pop_next_valid_pending_request()
{
    while (!pending_queue_.empty())
    {
        rotation_request_entry* req = pending_queue_.top();
        pending_queue_.pop();

        if (req->invalidated)
        {
            delete req;
            continue;
        }
        return req;
    }
    return nullptr;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ROTATION_SUBSYSTEM::get_new_active_qubit()
{
    // find the oldest request:
    auto oldest_it = request_map_.end();
    for (auto it = request_map_.begin(); it != request_map_.end(); it++)
    {
        if (it->second->done || it->second->allocated_qubit == nullptr || it->second->invalidated)
            continue;
        if (oldest_it != request_map_.end())
        {
            if (_request_compare(it->second, oldest_it->second))
                oldest_it = it;
        }
        else
        {
            oldest_it = it;
        }
    }

    if (oldest_it == request_map_.end())
    {
        active_qubit_ = nullptr;
    }
    else
    {
        active_qubit_ = oldest_it->second->allocated_qubit;
        active_qubit_->cycle_available = current_cycle() + 2;  // 2 cycle load latency
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

bool
_request_compare(const rotation_request_entry* a, const rotation_request_entry* b)
{
    if (a->critical && !b->critical)
        return true;
    else if (!a->critical && b->critical)
        return false;
    else
        return a->inst->number < b->inst->number;
}


} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
