/*
 *  author: Suhas Vittal
 *  date:   7 January 2026
 * */

#include "argparse.h"
#include "globals.h"
#include "sim.h"
#include "sim/client.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* CTXSIM VARIABLE AND FUNCTION DECLARATIONS */

namespace
{

using inst_ptr = INSTRUCTION*;

struct rotation_info
{
    inst_ptr origin{nullptr};
    size_t   gates_applied{0};
};

using rotation_buffer_type = std::vector<rotation_info>;

std::vector<sim::CLIENT*> CTX_CLIENTS;
std::vector<sim::QUBIT*>  CTX_QUBITS;
cycle_type                CTX_CURRENT_CYCLE{0};
size_t                    CTX_MAGIC_STATE_COUNT{0};
sim::CLIENT*              CTX_ACTIVE_CLIENT;

bool                              CTX_ROTATION_ELISION;
std::vector<rotation_buffer_type> CTX_ROTATION_BUFFER;

static std::uniform_real_distribution FPR(0.0,1.0);

/*
 * Initializes the CTX simulation.
 * */
void ctxsim_init(const std::vector<std::string>& trace_files, size_t rotation_buffer_capacity);

/*
 * Executes instruction for given client.
 * CTXSIM is a simple simulator, so we elide
 * H, S, CX, and CCX gates and only focus on 
 * T and RZ gates.
 * */
void ctxsim_read_and_execute_pending_instructions();

/*
 * Updates `CTX_MAGIC_STATE_COUNT` and sets the next available
 * cycle for the given QUBIT*. Returns true if the gate cna
 * be retired.
 * */
bool ctxsim_do_rotation_instruction(inst_ptr, sim::QUBIT*);
bool ctxsim_do_t_like_instruction(sim::QUBIT*);

/*
 * Sets `new_active_client` to `CTX_ACTIVE_CLIENT`.
 * Also models the delay.
 * */
void ctxsim_do_context_switch(sim::CLIENT* new_active_client);

void ctxsim_cleanup();

/*
 * Rotation elision functions:
 * */
void    ctxsim_re_serve();
ssize_t ctxsim_re_search_for_instruction(inst_ptr);

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

int
main(int argc, char* argv[])
{
    std::string trace_file;
    int64_t     ratemode;
    int64_t     inst_limit;
    int64_t     print_progress;

    double  magic_state_throughput;  // per cycle
    int64_t magic_state_capacity;
    int64_t max_cycles_before_context_switch;

    int64_t rotation_buffer_capacity;

    ARGPARSE()
        // simulation setup:
        .required("input-file", "The trace file to use", trace_file)
        .optional("-r", "--ratemode", "Number of clients", ratemode, 2)
        .optional("-i", "--inst-limit", "Number of simulation instructions", inst_limit, 100'000'000)
        .optional("-pp", "--print-progress", "Print progress cycles", print_progress, 1'000'000)
        
        // system configuration:
        .optional("-m", "--magic-state-throughput", "Number of magic states produced each cycle", magic_state_throughput, 1.0)
        .optional("", "--magic-state-capacity", "Max number of buffered magic states", magic_state_capacity, 32)
        .optional("-ctx", "--context-switch-frequency", "Max cycles before context switch is forced", max_cycles_before_context_switch, 1'000'000)

        // rotation elision:
        .optional("", "--rotation-elision", "Enable rotation elision", CTX_ROTATION_ELISION, false)
        .optional("", "--rotation-buffer-capacity", "Maximum number of rotations that can be buffered per client", rotation_buffer_capacity, 2)

        .parse(argc, argv);

    std::vector<std::string> trace_files(ratemode, trace_file);

    uint64_t s_context_switches{0};

    // simulation implementation:
    ctxsim_init(trace_files, rotation_buffer_capacity);

    double magic_state_prod{0.0};
    cycle_type last_context_switch_cycle{0};
    sim::GL_SIM_WALL_START = std::chrono::steady_clock::now();

    bool done;
    do
    {
        if (CTX_CURRENT_CYCLE % print_progress == 0)
        {
            std::cout << "CTXSIM, cycle = " << CTX_CURRENT_CYCLE
                        << " walltime = " << sim::walltime()
                        << " ------------------------------------------------------------------------\n";

            for (auto* c : CTX_CLIENTS)
            {
                std::cout << "client " << static_cast<int>(c->id) 
                            << " : inst done = " << c->s_unrolled_inst_done
                            << ", ipc = " << c->ipc()
                            << "\n";
            }
            std::cout << "context switches = " << s_context_switches
                        << "\n";
        }

        bool do_ctx_switch = (CTX_CURRENT_CYCLE - last_context_switch_cycle >= max_cycles_before_context_switch)
                                || (CTX_ACTIVE_CLIENT->s_unrolled_inst_done >= inst_limit);
        if (CTX_ROTATION_ELISION)
        {
            auto begin = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id].begin(),
                 end = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id].end();
            bool any_rotations_in_buffer = std::any_of(begin, end, [] (const auto& e) { return e.origin != nullptr; });
            do_ctx_switch |= !any_rotations_in_buffer;
        }

        // check if need to, and can, do a context switch
        if (do_ctx_switch)
        {
            auto c_it = std::find_if(CTX_CLIENTS.begin()+1, CTX_CLIENTS.end(),
                                [inst_limit] (auto* c) { return c->s_unrolled_inst_done < inst_limit; });
            if (c_it != CTX_CLIENTS.end())
            {
                ctxsim_do_context_switch(*c_it);
                last_context_switch_cycle = CTX_CURRENT_CYCLE;

                // we do this rotation so that the active client is always first.
                std::rotate(CTX_CLIENTS.begin(), c_it, CTX_CLIENTS.end());
                s_context_switches++;
            }
        }

        if (CTX_ROTATION_ELISION)
            ctxsim_re_serve();

        // execute pending gates if psosible
        ctxsim_read_and_execute_pending_instructions();

        // produce magic states:
        if (CTX_MAGIC_STATE_COUNT < magic_state_capacity)
        {
            magic_state_prod += magic_state_throughput;
            while (magic_state_prod > 0.9999999999)
            {
                magic_state_prod -= 1.0;
                CTX_MAGIC_STATE_COUNT++;

                // if we have hit capacity, then kill magic state production and exit this loop
                if (CTX_MAGIC_STATE_COUNT == magic_state_capacity)
                {
                    magic_state_prod = 0;
                    break;
                }
            }
        }

        CTX_CURRENT_CYCLE++;
        CTX_ACTIVE_CLIENT->s_cycle_complete = CTX_CURRENT_CYCLE;
        done = std::all_of(CTX_CLIENTS.begin(), CTX_CLIENTS.end(),
                        [inst_limit] (auto* c) { return c->s_unrolled_inst_done >= inst_limit; });
    }
    while (!done);

    /*
     * Print stats:
     * */
    double hmean_ipc = CTX_CLIENTS.size() / std::transform_reduce(CTX_CLIENTS.begin(), CTX_CLIENTS.end(),
                                            double{0.0},
                                            std::plus<double>{},
                                            [] (auto* c) { return 1.0/c->ipc(); });

    for (auto* c : CTX_CLIENTS)
    {
        std::string client_prefix = "CLIENT_" + std::to_string(c->id);
        print_stat_line(std::cout, client_prefix + "_IPC", c->ipc());
    }
    print_stat_line(std::cout, "CONTEXT_SWITCHES", s_context_switches);
    print_stat_line(std::cout, "HMEAN_IPC", hmean_ipc);


    ctxsim_cleanup();
    return 0;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/* CTXSIM IMPLEMENTATION STARTS HERE */

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ctxsim_init(const std::vector<std::string>& trace_files, size_t rotation_buffer_capacity)
{
    for (client_id_type i = 0; i < trace_files.size(); i++)
    {
        // create new client
        sim::CLIENT* c = new sim::CLIENT{trace_files[i], i};
        CTX_CLIENTS.push_back(c);
        
        // update `CTX_QUBITS`
        for (qubit_type q = 0; q < c->num_qubits; q++)
            CTX_QUBITS.push_back(new sim::QUBIT{.qubit_id=q, .client_id=i});

        // init rotation buffer if `CTX_ROTATION_ELISION` is sset
        if (CTX_ROTATION_ELISION)
            CTX_ROTATION_BUFFER.push_back(rotation_buffer_type(rotation_buffer_capacity));
    }

    CTX_ACTIVE_CLIENT = CTX_CLIENTS[0];
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ctxsim_read_and_execute_pending_instructions()
{
    // identify ready qubits belonging to the active client
    std::unordered_map<qubit_type, sim::QUBIT*> ready_qubits;
    ready_qubits.reserve(CTX_QUBITS.size());
    for (auto* q : CTX_QUBITS)
        if (q->client_id == CTX_ACTIVE_CLIENT->id && q->cycle_available <= CTX_CURRENT_CYCLE)
            ready_qubits.insert({q->qubit_id, q});

    // get all instruction's in the active client's front layer that have arguments
    // only among the `ready_qubits`
    auto front_layer = CTX_ACTIVE_CLIENT->get_ready_instructions(
                                [&ready_qubits] (auto* inst)
                                {
                                    return std::all_of(inst->q_begin(), inst->q_end(),
                                                [&ready_qubits] (auto q) { return ready_qubits.count(q) > 0; });
                                });

    for (auto* inst : front_layer)
    {
        sim::QUBIT* q = ready_qubits[inst->qubits[0]];

        bool retire;
        if (is_rotation_instruction(inst->type))
            retire = ctxsim_do_rotation_instruction(inst, q);
        else if (is_t_like_instruction(inst->type))
            retire = ctxsim_do_t_like_instruction(q);
        else
            retire = true;

        if (retire)
            CTX_ACTIVE_CLIENT->retire_instruction(inst);
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ctxsim_do_rotation_instruction(inst_ptr inst, sim::QUBIT* q)
{
    bool retire{false};
    
    if (CTX_ROTATION_ELISION)
    {
        ssize_t idx = ctxsim_re_search_for_instruction(inst);
        if (idx >= 0)
        {
            rotation_info& rot_data = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id][idx];

            // If success, then we can exit now since we elided properly.
            // Otherwise, assume thta the correction has the same number of
            // gates as the original rotation
            if (rot_data.gates_applied >= inst->uop_count())
            {
                retire = FPR(sim::GL_RNG) >= 0.5;
                q->cycle_available = CTX_CURRENT_CYCLE + 4;
            }
            rot_data.origin = nullptr;
            rot_data.gates_applied = 0;
        }
    }

    // get next uop that is a T gate:
    while (!retire && !is_t_like_instruction(inst->current_uop()->type))
        retire = inst->retire_current_uop();
    if (!retire && ctxsim_do_t_like_instruction(q))
        retire = inst->retire_current_uop();
    return retire;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

bool
ctxsim_do_t_like_instruction(sim::QUBIT* q)
{
    if (CTX_MAGIC_STATE_COUNT > 0)
    {
        CTX_MAGIC_STATE_COUNT--;
        q->cycle_available = CTX_CURRENT_CYCLE + 3;
        return true;
    }
    else
    {
        return false;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ctxsim_do_context_switch(sim::CLIENT* c)
{
    // update qubit availablity cycles of cur0rent client and `c`
    const cycle_type ctx_switch_latency = 75;
//  const cycle_type ctx_switch_latency = 0;

    // first get latest qubit availability
    cycle_type ctx_switch_start_cycle{0};
    for (sim::QUBIT* q : CTX_QUBITS)
        if (q->client_id == c->id || q->client_id == CTX_ACTIVE_CLIENT->id)
            ctx_switch_start_cycle = std::max(q->cycle_available, ctx_switch_start_cycle);
    
    // set all qubits' availability to `ctx_switch_end_Cycle`
    const cycle_type ctx_switch_end_cycle = ctx_switch_start_cycle + ctx_switch_latency;
    for (sim::QUBIT* q : CTX_QUBITS)
        if (q->client_id == c->id || q->client_id == CTX_ACTIVE_CLIENT->id)
            q->cycle_available = ctx_switch_end_cycle;

    if (CTX_ROTATION_ELISION)
    {
        // if we are doing rotation elision, then we need to find some rotations to install
        // into the rotation buffer:
        std::vector<inst_ptr> future_rotations;
        future_rotations.reserve(4);
        CTX_ACTIVE_CLIENT->dag()->for_each_instruction_in_layer_order(
                                    [&future_rotations] (inst_ptr inst)
                                    {
                                        if (is_rotation_instruction(inst->type))
                                            future_rotations.push_back(inst);
                                    }, 32);
        ssize_t idx = future_rotations.size()-1;
        auto& rb = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id];
        for (auto& e : rb)
        {
            if (idx < 0)
                break;
            if (e.origin == nullptr)
                e.origin = future_rotations[idx--];
        }
        std::reverse(rb.begin(), rb.end());
    }

    CTX_ACTIVE_CLIENT = c;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ctxsim_cleanup()
{
    for (auto* c : CTX_CLIENTS)
        delete c;
    for (auto* q : CTX_QUBITS)
        delete q;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
ctxsim_re_serve()
{
    for (auto* c : CTX_CLIENTS)
    {
        for (auto& e : CTX_ROTATION_BUFFER[c->id])
        {
            if (e.origin == nullptr || e.gates_applied >= e.origin->uop_count())
                continue;
            INSTRUCTION::TYPE t = e.origin->urotseq[e.gates_applied];
            if (is_t_like_instruction(t))
            {
                if (CTX_MAGIC_STATE_COUNT >= 2)
                {
                    CTX_MAGIC_STATE_COUNT--;
                    e.gates_applied++;
                }
            }
            else
            {
                e.gates_applied++;
            }
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

ssize_t
ctxsim_re_search_for_instruction(inst_ptr inst)
{
    auto begin = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id].begin(),
         end = CTX_ROTATION_BUFFER[CTX_ACTIVE_CLIENT->id].end();
    auto it = std::find_if(begin, end, [inst] (const auto& e) { return e.origin == inst; });
    if (it == end)
        return -1;
    else
        return std::distance(begin, it);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anon
