/*
 *  author: Suhas Vittal
 *  date:   12 January 2026
 * */

#include "sim/memory_subsystem.h"

#include <algorithm>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::MEMORY_SUBSYSTEM(std::vector<STORAGE*>&& storages)
    :storages_(std::move(storages))
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
MEMORY_SUBSYSTEM::tick()
{
    for (STORAGE* s : storages_)
        s->tick();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

MEMORY_SUBSYSTEM::access_result_type
MEMORY_SUBSYSTEM::do_memory_access(QUBIT* ld, QUBIT* st)
{
    auto storage_it = lookup(ld);
    if (storage_it == storages_.end())
    {
        std::cerr << "MEMORY_SUBSYSTEM::do_memory_access: qubit " << *ld << " not found";
        for (auto* s : storages_)
        {
            std::cerr << "\n\t" << s->name << " :";
            for (auto* q : s->contents())
                std::cerr << " " << *q;
        }
        std::cerr << _die{};
    }
    return (*storage_it)->do_memory_access(ld, st);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

const std::vector<STORAGE*>&
MEMORY_SUBSYSTEM::storages() const
{
    return storages_;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::vector<STORAGE*>::iterator
MEMORY_SUBSYSTEM::lookup(QUBIT* q)
{
    return std::find_if(storages_.begin(), storages_.end(),
        [q] (STORAGE* s) { return s->contents().count(q) > 0; });
}

std::vector<STORAGE*>::const_iterator
MEMORY_SUBSYSTEM::lookup(QUBIT* q) const
{
    return std::find_if(storages_.begin(), storages_.end(),
        [q] (STORAGE* s) { return s->contents().count(q) > 0; });
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

} // namespace sim
