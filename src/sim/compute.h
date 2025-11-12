/*
    author: Suhas Vittal
    date:   15 September 2025
*/

#ifndef SIM_COMPUTE_h
#define SIM_COMPUTE_h

#include "sim/client.h"
#include "sim/factory.h"
#include "sim/memory.h"
#include "sim/operable.h"
#include "sim/routing.h"

#include <deque>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
#include <random>

namespace sim
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

enum class COMPUTE_EVENT_TYPE
{
    MAGIC_STATE_AVAIL,
    MEMORY_ACCESS_DONE,
    INST_EXECUTE,
    INST_COMPLETE
};

struct COMPUTE_EVENT_INFO
{
    int8_t           client_id;
    CLIENT::inst_ptr inst;

    // for memory access events:
    QUBIT    mem_accessed_qubit;
    QUBIT    mem_victim_qubit;
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

class COMPUTE : public OPERABLE<COMPUTE_EVENT_TYPE, COMPUTE_EVENT_INFO>
{
public:
    using typename OPERABLE<COMPUTE_EVENT_TYPE, COMPUTE_EVENT_INFO>::event_type;

    using client_ptr = std::unique_ptr<CLIENT>;
    using inst_ptr = CLIENT::inst_ptr;
    using inst_window_type = std::deque<inst_ptr>;
    using rename_table_type = std::unordered_map<qubit_type, qubit_type>;

    struct exec_result_type
    {
        bool       is_memory_stall{false};
        bool       is_resource_stall{false};
        uint64_t   routing_stall_cycles{0};
        uint64_t   cycles_until_done{std::numeric_limits<uint64_t>::max()};
    };

    // statistics:
    uint64_t s_evictions_no_uses{0};
    uint64_t s_evictions_prefetch_no_uses{0};

    uint64_t s_operations_with_decoupled_loads{0};

    const size_t target_t_fact_level_;
    const size_t num_rows_;
    const size_t num_patches_per_row_;
private:
    std::vector<client_ptr> clients_;

    // track when qubits become available:
    std::unordered_map<QUBIT, uint64_t> qubit_available_cycle_;
    
    // track instruction windows for each qubit to manage dependencies:
    std::unordered_map<QUBIT, inst_window_type> inst_windows_;
    
    // track qubit renamings for each client:
    std::vector<rename_table_type> rename_tables_;

    // instructions that are waiting for memory accesses and resources states:
    // entries are the instruction pointer and the client id
    std::vector<std::pair<inst_ptr, int8_t>> inst_waiting_for_memory_;  // use a vector here as we will remove elements arbitrarily
    std::deque<std::pair<inst_ptr, int8_t>> inst_waiting_for_resource_;  // use a deque here as we will only remove the first element

    // patches on the compute substrate
    std::vector<PATCH> patches_;

    // magic state factories and memory modules:
    std::vector<T_FACTORY*> t_fact_;
    std::vector<MEMORY_MODULE*> mem_modules_;

    // useful variables for initialization:
    // patch indices:
    size_t compute_start_idx_;
    size_t memory_start_idx_;
public:
    COMPUTE(double freq_khz, 
            std::vector<std::string> client_trace_files,
            size_t num_rows, 
            size_t patches_per_row,
            std::vector<T_FACTORY*>,
            std::vector<MEMORY_MODULE*>);

    // returns how long it takes to complete the memory access (ns)
    uint64_t route_memory_access(size_t mem_patch_idx, QUBIT victim, uint64_t module_cycle_free);
    void update_state_after_memory_access(QUBIT loaded, QUBIT stored, uint64_t cycle_done, bool is_prefetch);

    void OP_init() override;

    void dump_deadlock_info();

    void reassign_decoupled_store(INSTRUCTION*, QUBIT);  // this occurs when there are no locations to
                                                         // perform a decoupled store
    bool has_any_empty_program_patches() const;

    // Check for duplicate qubits across compute patches, memory banks, and decoupled loads
    bool check_for_duplicates() const;

    // exposing some data to user:
    const std::vector<client_ptr>& get_clients() const { return clients_; }
    bool                           is_present_in_compute(QUBIT q) const { return find_patch_containing_qubit_c(q) != patches_.end(); }
    uint64_t                       get_cycle_free(QUBIT q) const { return qubit_available_cycle_.at(q); }
    bool                           has_empty_instruction_window(QUBIT q) const { return inst_windows_.find(q) == inst_windows_.end() || inst_windows_.at(q).empty(); }
    const std::deque<inst_ptr>&    get_instruction_window(QUBIT q) const { return inst_windows_.at(q); }
    size_t                         get_num_uses_in_compute(QUBIT q) const { return find_patch_containing_qubit_c(q)->num_uses; }

    std::vector<T_FACTORY*> get_t_factories() const { return t_fact_; }
    std::vector<MEMORY_MODULE*> get_mem_modules() const { return mem_modules_; }
protected:
    void OP_handle_event(event_type) override;
private:
    using routing_info = std::pair<std::vector<ROUTING_COMPONENT::ptr_type>, std::vector<ROUTING_COMPONENT::ptr_type>>;

    routing_info con_init_routing_space();
    void         con_init_patches(routing_info);
    void         con_init_clients(std::vector<std::string> client_trace_files);

    void client_fetch(client_ptr&);
    void client_schedule(client_ptr&);
    void client_execute(client_ptr&, inst_ptr);
    void client_retire(client_ptr&, inst_ptr);

    exec_result_type execute_instruction(client_ptr&, inst_ptr);
    void             process_execution_result(client_ptr&, inst_ptr, exec_result_type);

    exec_result_type do_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);

    exec_result_type do_sw_gate(client_ptr&, inst_ptr);
    exec_result_type do_h_or_s_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);
    exec_result_type do_t_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);
    exec_result_type do_cx_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);
    exec_result_type do_rz_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);
    exec_result_type do_ccx_gate(client_ptr&, inst_ptr, std::vector<PATCH*>);
    
    exec_result_type do_memory_instruction(client_ptr&, inst_ptr);
    
    // retries the execution of instructions in the given buffer provided the appropriate resources are available
    void retry_memory_stalled_instructions(COMPUTE_EVENT_INFO);
    void retry_resource_stalled_instructions(COMPUTE_EVENT_INFO);

    std::vector<PATCH>::iterator find_patch_containing_qubit(QUBIT);
    std::vector<PATCH>::const_iterator find_patch_containing_qubit_c(QUBIT) const;
    std::vector<MEMORY_MODULE*>::iterator find_memory_module_containing_qubit(QUBIT);

    std::vector<MEMORY_MODULE*>::iterator find_memory_module_with_pending_store_for_qubit(QUBIT);

    std::vector<PATCH>::iterator find_epr_generator_containing_qubit(QUBIT);

    inst_ptr try_decoupling_load_store(inst_ptr, int8_t client_id);
};

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void update_free_times_along_routing_path(std::vector<ROUTING_COMPONENT::ptr_type>& path, 
                                            uint64_t cmp_cycle_free_bulk, 
                                            uint64_t cmp_cycle_free_endpoints);

bool is_t_like_instruction(INSTRUCTION::TYPE);

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}   // namespace sim

#endif  // SIM_COMPUTE_h
