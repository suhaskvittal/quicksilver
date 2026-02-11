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

rotation_request_entry* _get_next_valid_request_in_linked_list(rotation_request_entry*);

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ROTATION_SUBSYSTEM::ROTATION_SUBSYSTEM(double freq_khz,
                                        size_t capacity,
                                        COMPUTE_SUBSYSTEM* parent,
                                        double watermark)
    :COMPUTE_BASE("rotation_subsystem", 
                    freq_khz, 
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
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

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
    out << "\n\tactive qubit = " << *active_qubit_
        << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ROTATION_SUBSYSTEM::can_accept_request() const
{
    return !free_qubits_.empty();
}

bool
ROTATION_SUBSYSTEM::submit_request(inst_ptr inst, size_t layer, inst_ptr triggering_inst)
{
    batch_request_info tmp{.inst=inst, .dag_layer=layer};
    return submit_batch_request({tmp}, triggering_inst);
}

bool
ROTATION_SUBSYSTEM::submit_batch_request(std::vector<batch_request_info> reqs, inst_ptr triggering_inst)
{
    if (reqs.empty())
        return true;
    if (free_qubits_.empty())
        return false;

    rotation_request_entry* prev{nullptr};
    for (const auto& r : reqs)
    {
        if (is_request_pending(r.inst))
            continue;

        rotation_request_entry* req = new rotation_request_entry
                                        {
                                            .inst=r.inst,
                                            .dag_layer=r.dag_layer,
                                            .triggering_inst_info=triggering_inst->to_string()
                                        };
        if (prev == nullptr)
        {
            QUBIT* q = free_qubits_.back();
            free_qubits_.pop_back();
            req->allocated_qubit = q;
            if (active_qubit_ == nullptr)
                active_qubit_ = q;
        }
        else
        {
            prev->next_request = req;
        }

        request_map_[r.inst] = req;
        prev = req;
    }

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
ROTATION_SUBSYSTEM::invalidate(inst_ptr inst)
{
    auto it = request_map_.find(inst);
    if (it == request_map_.end())
        return;

    auto* req = it->second;
    req->invalidated = true;
    request_map_.erase(it);
    s_invalidates++;

    // if this is the head of the linked list, then delete:
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

    const size_t num_teleports = (inst->rpc_is_critical || GL_RPC_RS_ALWAYS_USE_TELEPORTATION) 
                                    ? GL_T_GATE_TELEPORTATION_MAX
                                    : 0;

    auto result = do_rotation_gate_with_teleportation_while_predicate_holds(inst, {q}, num_teleports,
                    [this] (const inst_ptr x, const inst_ptr uop)
                    {
                        const size_t m = count_available_magic_states();
                        const size_t min_t_count = 1;
                        return x->rpc_is_critical || (m > min_t_count);
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

bool
ROTATION_SUBSYSTEM::delete_request(rotation_request_entry* req)
{
    auto* next_req = _get_next_valid_request_in_linked_list(req);
    if (next_req != nullptr)
        next_req->allocated_qubit = req->allocated_qubit;
    else
        free_qubits_.push_back(req->allocated_qubit);

    // if `allocated_qubit` is active, then move a different qubit into the active region:
    if (req->allocated_qubit == active_qubit_)
        get_new_active_qubit();
    delete req;
    return (next_req == nullptr);
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
            const size_t d1 = it->second->dag_layer,
                         d2 = oldest_it->second->dag_layer;
            const auto i1 = it->first->number,
                       i2 = oldest_it->first->number;
            if (d1 < d2 || (d1 == d2 && i1 < i2))
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

        /*
        if (request_map_.size() > 1)
        {
            std::cout << "new active qubit: " << *active_qubit_
                    << " for inst = " << *oldest_it->first
                    << "\ncandidates:";
            for (const auto& [inst, req] : request_map_)
            {
                if (req->done || req->invalidated)
                    continue;
                std::cout << "\n\t" << *inst;
            }
            std::cout << "\n";
        }
        */
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace
{

rotation_request_entry*
_get_next_valid_request_in_linked_list(rotation_request_entry* req)
{
    auto* next_req = req->next_request;
    while (next_req != nullptr && next_req->invalidated)
    {
        auto* next_next_req = next_req->next_request;
        delete next_req;
        next_req = next_next_req;
    }
    return next_req;
}

} // namespace anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
