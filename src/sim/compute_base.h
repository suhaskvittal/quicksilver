/*
 *  author: Suhas Vittal
 *  date:   13 January 2026
 * */

#ifndef SIM_COMPUTE_BASE_h
#define SIM_COMPUTE_BASE_h

#include "sim/client.h"
#include "sim/factory.h"
#include "sim/storage.h"

#include <array>
#include <memory>
#include <vector>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class MEMORY_SUBSYSTEM;

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE_BASE : public OPERABLE
{
public:
    using inst_ptr = CLIENT::inst_ptr;

    const size_t local_memory_capacity;
protected:
    std::unique_ptr<STORAGE>     local_memory_;
    std::vector<T_FACTORY_BASE*> top_level_t_factories_;
    MEMORY_SUBSYSTEM*            memory_hierarchy_;
public:
    COMPUTE_BASE(std::string_view             name, 
                 double                       freq_khz,
                 size_t                       local_memory_capacity,
                 std::vector<T_FACTORY_BASE*> top_level_t_factories,
                 MEMORY_SUBSYSTEM*            memory_hierarchy);

    const std::unique_ptr<STORAGE>&     local_memory() const;
    const std::vector<T_FACTORY_BASE*>& top_level_t_factories() const;
    MEMORY_SUBSYSTEM*                   memory_hierarchy() const;
protected:
    virtual bool execute_instruction(inst_ptr, std::array<QUBIT*, 3>&& args);

    virtual bool do_h_or_s_gate(inst_ptr, QUBIT*);
    virtual bool do_cx_like_gate(inst_ptr, QUBIT* ctrl, QUBIT* target);
    virtual bool do_t_like_gate(inst_ptr, QUBIT*);
    virtual bool do_memory_access(inst_ptr, QUBIT* ld, QUBIT* st);

    size_t count_available_magic_states() const;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // namespace sim

#endif  // SIM_COMPUTE_BASE_h
