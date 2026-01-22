/*
    author: Suhas Vittal
    date:   22 August 2025
*/

#include "compiler/program.h"
#include "compiler/program/expression.h"
#include "compiler/program/oq2/lexer_wrapper.h"
#include "compiler/program/rotation_manager.h"
#include "compiler/program/value_info.h"
#include "parser.tab.h"

#include <cstdio>
#include <deque>
#include <initializer_list>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

// `DROP_MEASUREMENT_GATES` is necessary for many QASMBench workloads, since they
// have invalid measurement syntax
#define DROP_MEASUREMENT_GATES
#define ALLOW_GATE_DECL_OVERRIDES

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

int64_t GL_PRINT_PROGRESS{1'000'000};

} // namespace prog

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using namespace prog;

/*
 * Helper functions and other useful data structures
 * */

namespace
{

constexpr static INSTRUCTION::TYPE SELF_INVERSES[]
{
    INSTRUCTION::TYPE::H,
    INSTRUCTION::TYPE::X,
    INSTRUCTION::TYPE::Y,
    INSTRUCTION::TYPE::Z,
    INSTRUCTION::TYPE::CX,
    INSTRUCTION::TYPE::CZ,
    INSTRUCTION::TYPE::CCX,
    INSTRUCTION::TYPE::CCZ
};

/*
 * `subst_map_type` implements a substitution map (aliases).
 * In our codebase, we care about parameter and argument substitution.
 * */
template <class T> using subst_map_type = std::unordered_map<std::string, T>;

std::string _qasm_inst_to_string(const prog::QASM_INST_INFO&);

/*
 * `_make_substitution_map` creates a dictionary mapping entries in `names`
 * to `values` elementwise. An error is thrown if `names.size() != values.size()`.
 *
 * `qasm_inst_str` is only needed for the error msg.
 * */
template <class T>
subst_map_type<T> _make_substitution_map(const std::vector<std::string>& names,
                                         const std::vector<T>& values,
                                         std::string_view qasm_inst_str);

/*
 * `_parameter_substitution` and `_argument_substitution` perform substitutions for
 * any strings internal to an expression or operand.
 *
 * For example, if `X = pi/2` and the expression is `2*X+3`, then `_parameter_substitution`
 * will update this to be `2*(pi/2)+3`
 * */
void _parameter_substitution(EXPRESSION&, const subst_map_type<EXPRESSION>&);
void _argument_substitution(prog::QASM_OPERAND&, const subst_map_type<prog::QASM_OPERAND>&);

/*
 * Checks if two maps have the same key. If so, the programs exits with an error.
 * `dupli_name` is only needed for the error message.
 * */
template <class MAP_TYPE>
void _scan_and_die_on_conflict(const MAP_TYPE&, const MAP_TYPE&, std::string_view dupli_name);

/*
 * Computes a hashtable containing gates and their inverses. For example, T <--> TDG.
 * */
std::unordered_map<INSTRUCTION::TYPE, INSTRUCTION::TYPE> _make_inverse_map();

/*
 * Returns the required gridsynth precision to accurate approximately
 * the given angle.
 * */
size_t _get_required_precision(const INSTRUCTION::fpa_type&);

const auto GATE_INVERSE_MAP{_make_inverse_map()};

} // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::stats_type::merge(const stats_type& other)
{
    total_gate_count += other.total_gate_count;
    software_gate_count += other.software_gate_count;
    t_gate_count += other.t_gate_count;
    cxz_gate_count += other.cxz_gate_count;
    rotation_count += other.rotation_count;
    ccxz_count += other.ccxz_count;
    virtual_inst_count += other.virtual_inst_count;
    unrolled_inst_count += other.unrolled_inst_count;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO::PROGRAM_INFO(generic_strm_type* ostrm_p)
    :ostrm_p_(ostrm_p)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO
PROGRAM_INFO::from_file(std::string input_file)
{
    PROGRAM_INFO prog{nullptr};

    generic_strm_type istrm;
    generic_strm_open(istrm, input_file, "rb");

    std::istringstream _tmp{};
    OQ2_LEXER lexer(_tmp, &istrm);
    yy::parser parser(lexer, prog, "");
    [[ maybe_unused ]] int retcode = parser();

    generic_strm_close(istrm);

    prog.final_stats = prog.compute_statistics_for_current_instructions();

    return prog;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO::stats_type
PROGRAM_INFO::read_from_file_and_write_to_binary(std::string input_file, std::string output_file)
{
    generic_strm_type ostrm;
    generic_strm_open(ostrm, output_file, "wb");

    PROGRAM_INFO prog(&ostrm);

    // get the dirname of `input_file`
    std::string dirname = input_file.substr(0, input_file.find_last_of('/'));

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] reading file: " << input_file << ", new relative path: " << dirname << "\n";
#endif

    generic_strm_type istrm;
    generic_strm_open(istrm, input_file, "rb");

    std::istringstream _tmp{};
    OQ2_LEXER lexer(_tmp, &istrm);
    yy::parser parser(lexer, prog, dirname);
    [[ maybe_unused ]] int retcode = parser();

    generic_strm_close(istrm);

    // need to get last set of stats and merge:
    prog.flush_and_clear_instructions();

    return prog.final_stats;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::add_instruction(QASM_INST_INFO&& qasm_inst)
{
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] qasm_inst: " << qasm_inst_to_string(qasm_inst) << "\n";
#endif

    // Handle gate aliases
    if (qasm_inst.gate_name == "measure")
        qasm_inst.gate_name = "mz";

    // Drop certain gates
#if defined(DROP_MEASUREMENT_GATES)
    if (qasm_inst.gate_name == "mz" || qasm_inst.gate_name == "mx")
        return;
#endif
    if (qasm_inst.gate_name == "barrier")
        return;

    // Check if basis gate
    auto basis_gate_it = std::find(std::begin(BASIS_GATES), std::end(BASIS_GATES), qasm_inst.gate_name);
    if (basis_gate_it != std::end(BASIS_GATES))
    {
        auto type = static_cast<INSTRUCTION::TYPE>(std::distance(std::begin(BASIS_GATES), basis_gate_it));
        add_basis_gate_instruction(std::move(qasm_inst), type);
    }
    else
    {
        expand_user_defined_gate(std::move(qasm_inst));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::declare_register(REGISTER&& reg)
{
    auto it = registers_.find(reg.name);
    if (it != registers_.end())
        throw std::runtime_error("register already declared: " + reg.name);

    if (reg.type == REGISTER::TYPE::QUBIT)
    {
        reg.id_offset = num_qubits_declared_;
        num_qubits_declared_ += reg.width;
    }
    else
    {
        reg.id_offset = num_bits_declared_;
        num_bits_declared_ += reg.width;
    }

    registers_.insert({reg.name, std::move(reg)});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::declare_gate(GATE_DEFINITION&& gate_def)
{
#if !defined(ALLOW_GATE_DECL_OVERRIDES)
    auto it = user_defined_gates_.find(gate_def.name);
    if (it != user_defined_gates_.end())
        std::cerr << "PROGRAM_INFO::declare_gate: gate already declared: " << gate_def.name << _die{};
#endif

    user_defined_gates_.insert({gate_def.name, std::move(gate_def)});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::merge(PROGRAM_INFO&& other)
{
    // merge stats:
    other.final_stats.merge(other.compute_statistics_for_current_instructions());
    final_stats.merge(other.final_stats);

    std::cout << "[ PROGRAM_INFO ] post merge counts:"
        << "\tvirtual inst = " << other.final_stats.virtual_inst_count
        << "\tunrolled inst = " << other.final_stats.unrolled_inst_count
        << "\n";

    // merge data structures (`registers_` and `user_defined_gates_`):
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] merging registers and user-defined gates from external file\n";
#endif
    
    // first check for name conflicts:
    _scan_and_die_on_conflict(registers_, other.registers_, "register");
    _scan_and_die_on_conflict(user_defined_gates_, other.user_defined_gates_, "gate");

#if defined(PROGRAM_INFO_VERBOSE)
    for (const auto& [name, reg] : other.registers_)
        std::cout << "\tnew register: " << name << ", width: " << reg.width << "\n";
    for (const auto& [name, gate] : other.user_defined_gates_)
        std::cout << "\tnew gate decl: " << name << "\n";
#endif

    // now we can merge (nothing will be missing/overwritten)
    registers_.merge(std::move(other.registers_));
    user_defined_gates_.merge(std::move(other.user_defined_gates_));

    // merge instructions: 
    instructions_.reserve(instructions_.size() + other.instructions_.size());
    std::move(std::make_move_iterator(other.instructions_.begin()),
              std::make_move_iterator(other.instructions_.end()), 
              std::back_inserter(instructions_));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
PROGRAM_INFO::dead_gate_elimination()
{
    return dead_gate_elim_pass();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::flush_and_clear_instructions()
{
    complete_rotation_gates();

    std::cout << "[ PROGRAM_INFO ] flushing instructions to file\n";
    // first, do optimizations:
    [[ maybe_unused ]] size_t num_gates_removed = dead_gate_elimination();
    std::cout << "[ PROGRAM_INFO ] done with optimizations, removed " << num_gates_removed << " gates\n";

    // update stats first while we have `instructions_`
    auto curr_stats = compute_statistics_for_current_instructions();
    final_stats.merge(curr_stats);

    std::cout << "[ PROGRAM_INFO ] rotation count: " << final_stats.rotation_count
                << "\n[ PROGRAM_INFO ] unrolled instruction count: " << final_stats.unrolled_inst_count
                << "\n[ PROGRAM_INFO ] virtual instruction count: " << final_stats.virtual_inst_count 
                << "\n";

    // write to file:
    // if number of qubits has not been written yet, do it now:
    if (!has_qubit_count_been_written_)
    {
        uint32_t num_qubits = num_qubits_declared_;
        generic_strm_write(*ostrm_p_, &num_qubits, sizeof(num_qubits));
        has_qubit_count_been_written_ = true;
    }

    for (const auto& inst : instructions_)
    {
        // retrieve the rotation sequence for this instruction:
        if (is_rotation_instruction(inst->type) && inst->urotseq.empty())
            continue;
        write_instruction_to_stream(*ostrm_p_, inst.get());
    }

    // clear instructions:
    instructions_.clear();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

qubit_type
PROGRAM_INFO::get_qubit_id_from_operand(const prog::QASM_OPERAND& operand) const
{
    // get register:
    auto it = registers_.find(operand.name);
    if (it == registers_.end())
        throw std::runtime_error("register not found: " + operand.name);

    const auto& r = it->second;
    qubit_type idx = r.id_offset;

    // verify that the offset is within the register:
    if (operand.index >= 0 && operand.index >= r.width)
    {
        std::cerr << "PROGRAM_INFO::get_qubit_id_from_operand: operand idx out of bounds: "
                    << operand.name << "[" << operand.index << "]" << _die{};
    }

    return idx + std::max(operand.index, ssize_t{0});  // handle the case where the index is negative (NO_INDEX)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::fpa_type
PROGRAM_INFO::process_rotation_gate(INSTRUCTION::TYPE type, const EXPRESSION& angle_expr)
{
    // Given our basis gates, there can only be one parameter for rotation gates.
    fpa_type rotation = evaluate_expression(angle_expr).readout_fixed_point_angle();

    // ignore gates with an angle of 0:
    if (rotation.popcount() == 0)
        return fpa_type{};  // return zero angle
                            
    // schedule the rotation's synthesis:
    rotation_manager_schedule_synthesis(rotation, _get_required_precision(rotation));
    for (size_t i = 0; i < GL_USE_RPC_ISA; i++)
    {
        auto corrective_rotation = fpa::scalar_mul(rotation, 2*(i+1));
        rotation_manager_schedule_synthesis(corrective_rotation, _get_required_precision(corrective_rotation));
    }

    return rotation;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::add_scalar_instruction(INSTRUCTION::TYPE type, 
                                      const std::vector<prog::QASM_OPERAND>& args, 
                                      fpa_type rotation)
{
    // convert operands to qubits:
    std::vector<qubit_type> qubits(args.size());
    std::transform(args.begin(), args.end(), qubits.begin(),
                [this] (const auto& x) { return this->get_qubit_id_from_operand(x); });

    std::vector<INSTRUCTION::TYPE> urotseq;
    inst_ptr inst{new INSTRUCTION{type, qubits.begin(), qubits.end(), rotation, urotseq.begin(), urotseq.end()}};
    instructions_.push_back(std::move(inst));

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "\tevaluated as: " << inst << "\n";
#endif
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::add_vector_instruction(INSTRUCTION::TYPE type, prog::QASM_INST_INFO& qasm_inst, fpa_type rotation,
                                      size_t width, const std::vector<bool>& v_op_vec, const std::vector<size_t>& v_op_width)
{
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "\tevaluated as vector instruction, expanded as:\n";
#endif

    // apply the operation multiple times:
    std::vector<qubit_type> qubits(qasm_inst.args.size());
    std::vector<INSTRUCTION::TYPE> urotseq;
    for (size_t i = 0; i < width; i++)
    {
        // set all vector operands to use index `i`
        for (size_t j = 0; j < qasm_inst.args.size(); j++)
        {
            if (v_op_vec[j])
            {
                assert(v_op_width[j] == width);
                qasm_inst.args[j].index = i;
            }
            qubits[j] = get_qubit_id_from_operand(qasm_inst.args[j]);
        }

        // create and push the instruction:
        inst_ptr inst{new INSTRUCTION{type, qubits.begin(), qubits.end(), rotation, urotseq.begin(), urotseq.end()}};
        instructions_.push_back(std::move(inst));

#if defined(PROGRAM_INFO_VERBOSE)
        std::cout << "\t\t( " << i << " ) " << inst << "\n";
#endif
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::expand_user_defined_gate(prog::QASM_INST_INFO&& qasm_inst)
{
    // check for gate definition:
    auto gate_it = user_defined_gates_.find(qasm_inst.gate_name);
    if (gate_it == user_defined_gates_.end())
        std::cerr << "PROGRAM_INFO::expand_user_defined_gate: gate not defined " << qasm_inst.gate_name << _die{};

    // get gate definition:
    const auto& gate_def = gate_it->second;
    if (gate_def.body.empty())
        return;   // this is a NOP

    auto param_subst_map = _make_substitution_map(gate_def.params, qasm_inst.params, qasm_inst.gate_name);
    auto arg_subst_map = _make_substitution_map(gate_def.args, qasm_inst.args, qasm_inst.gate_name);
    for (const auto& q_inst : gate_def.body)
    {
        QASM_INST_INFO inst = q_inst;
        for (auto& p : inst.params)
            _parameter_substitution(p, param_subst_map);
        for (auto& x : inst.args)
            _argument_substitution(x, arg_subst_map);
        add_instruction(std::move(inst));
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::add_basis_gate_instruction(prog::QASM_INST_INFO&& qasm_inst, INSTRUCTION::TYPE type)
{
    if (inst_read_ % GL_PRINT_PROGRESS == 0)
        std::cout << "[ PROGRAM_INFO ] read " << inst_read_ << " instructions\n";
    inst_read_++;

    // Handle rotations
    fpa_type rotation{};
    if (is_rotation_instruction(type))
    {
        rotation = process_rotation_gate(type, qasm_inst.params[0]);
        if (rotation.popcount() == 0)
            return;  // skip zero-angle rotations
    }

    // Check for vector operands
    std::vector<bool> v_op_vec(qasm_inst.args.size(), false);
    std::vector<size_t> v_op_width(qasm_inst.args.size(), 0);
    for (size_t i = 0; i < qasm_inst.args.size(); i++)
    {
        const auto& operand = qasm_inst.args[i];
        auto it = registers_.find(operand.name);
        if (it == registers_.end())
            std::cerr << "PROGRAM_INFO::add_basis_gate_instruction: register not found: " << operand.name << _die{};

        v_op_vec[i] = (it->second.width > 1) && (operand.index == prog::QASM_OPERAND::NO_INDEX);
        v_op_width[i] = it->second.width;
    }

    // check if any of the operands are vector operands.
    auto v_op_it = std::find(v_op_vec.begin(), v_op_vec.end(), true);
    if (v_op_it != v_op_vec.end())
    {
        // treat as vector instruction -- all vector operands must have same width so we can use `*v_op_it`
        size_t first_vec_idx = std::distance(v_op_vec.begin(), v_op_it);
        size_t width = v_op_width[first_vec_idx];
        add_vector_instruction(type, qasm_inst, rotation, width, v_op_vec, v_op_width);
    }
    else
    {
        add_scalar_instruction(type, qasm_inst.args, rotation);
    }

    // check if `instructions_` is too large: if so, flush to output file:
    if (instructions_.size() >= MAX_INST_BEFORE_FLUSH && ostrm_p_ != nullptr)
        flush_and_clear_instructions();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::cancel_adjacent_rotations()
{
    size_t i{1};
    while (i < instructions_.size())
    {
        auto& prev_inst = instructions_[i-1];
        auto& curr_inst = instructions_[i];

        if (is_rotation_instruction(curr_inst->type) && is_rotation_instruction(prev_inst->type))
        {
            auto angle_sum = fpa::add(curr_inst->angle, prev_inst->angle);
            if (angle_sum.popcount() == 0)
            {
                prev_inst->deletable = true;
                curr_inst->deletable = true;
                i += 2;
                continue;
            }
        }
        i++;
    }
}

void
PROGRAM_INFO::cancel_inverse_gate_pairs()
{
    size_t i{1};
    while (i < instructions_.size())
    {
        auto& prev_inst = instructions_[i-1];
        auto& curr_inst = instructions_[i];

        auto it = GATE_INVERSE_MAP.find(curr_inst->type);
        if (it != GATE_INVERSE_MAP.end() && prev_inst->type == it->second)
        {
            // Check that all qubits match
            bool same{true};
            for (size_t j = 0; j < curr_inst->qubit_count; ++j)
                same &= (curr_inst->qubits[j] == prev_inst->qubits[j]);

            if (same)
            {
                prev_inst->deletable = true;
                curr_inst->deletable = true;
                i += 2; 
                continue;
            }
        }
        i++;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
PROGRAM_INFO::dead_gate_elim_pass(size_t prev_gates_removed)
{
    size_t num_gates_before = instructions_.size();

    // Phase 1: Remove zero-angle rotations
    auto it = std::remove_if(instructions_.begin(), instructions_.end(),
                        [] (const auto& inst)
                        {
                            return is_rotation_instruction(inst->type) && inst->angle.popcount() == 0;
                        });
    instructions_.erase(it, instructions_.end());

    // Phase 2: Remove adjacent inverse pairs
    cancel_adjacent_rotations();
    cancel_inverse_gate_pairs();

    // Phase 3: Remove NIL gates
    it = std::remove_if(instructions_.begin(), instructions_.end(), [] (const auto& inst) { return inst->deletable; });
    instructions_.erase(it, instructions_.end());

    size_t removed_this_pass = num_gates_before - instructions_.size();

    // Recurse if we made progress
    if (removed_this_pass == 0)
        return prev_gates_removed;
    else
        return dead_gate_elim_pass(prev_gates_removed + removed_this_pass);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO::stats_type
PROGRAM_INFO::compute_statistics_for_current_instructions() const
{
    stats_type out{};

    for (const auto& inst : instructions_)
    {
        bool is_sw_gate = is_software_instruction(inst->type);
        bool is_t_like = (inst->type == INSTRUCTION::TYPE::T || inst->type == INSTRUCTION::TYPE::TDG);
        bool is_cxz = (inst->type == INSTRUCTION::TYPE::CX || inst->type == INSTRUCTION::TYPE::CZ);
        bool is_rot = (inst->type == INSTRUCTION::TYPE::RX || inst->type == INSTRUCTION::TYPE::RZ);
        bool is_ccxz = (inst->type == INSTRUCTION::TYPE::CCX || inst->type == INSTRUCTION::TYPE::CCZ);

        out.total_gate_count++;
        out.software_gate_count += is_sw_gate;
        out.t_gate_count += is_t_like;
        out.cxz_gate_count += is_cxz;
        out.rotation_count += is_rot;
        out.ccxz_count += is_ccxz;

        out.virtual_inst_count++;
        out.unrolled_inst_count += inst->unrolled_inst_count();
    }

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::complete_rotation_gates()
{
    size_t ii{0};
    for (auto& inst : instructions_)
    {
        if (ii % 100'000 == 0)
            (std::cout << ".").flush();
        ii++;
        if (is_rotation_instruction(inst->type))
        {
            inst->urotseq = retrieve_urotseq(inst->angle);
            if (inst->urotseq.empty())
                std::cerr << "[alert] rotation synthesis yielded empty sequence for " << *inst << "\n";

            // handle corrective rotations
            for (size_t i = 0; i < GL_USE_RPC_ISA; i++)
            {
                auto corrective_angle = fpa::scalar_mul(inst->angle, 2*(i+1));
                inst->corr_urotseq_array.push_back(retrieve_urotseq(corrective_angle));
            }
        }
    }
    std::cout << "\n";
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

INSTRUCTION::urotseq_type
PROGRAM_INFO::retrieve_urotseq(const fpa_type& angle)
{
    auto it = rotation_cache_.find(angle);
    if (it == rotation_cache_.end()) // miss
    {
        auto urotseq = rotation_manager_find(angle, _get_required_precision(angle));
        rotation_cache_.insert({angle, urotseq});
        return urotseq;
    }
    else
    {
        return it->second;
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

/** HELPER FUNCTIONS START HERE **/

namespace
{

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::string
_qasm_inst_to_string(const QASM_INST_INFO& inst)
{
    std::stringstream ss;
    std::stringstream gate_param_ss;

    gate_param_ss << inst.gate_name;
    if (!inst.params.empty())
    {
        gate_param_ss << "( ";
        for (size_t i = 0; i < inst.params.size(); ++i)
        {
            if (i > 0)
                gate_param_ss << ", ";
            gate_param_ss << inst.params[i].to_string();
        }
        gate_param_ss << " )";
    }

    ss << std::left << std::setw(24) << gate_param_ss.str();

    for (size_t i = 0; i < inst.args.size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << inst.args[i].name;
        if (inst.args[i].index != prog::QASM_OPERAND::NO_INDEX)
            ss << "[" << inst.args[i].index << "]";
    }

    return ss.str();
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> std::unordered_map<std::string, T>
_make_substitution_map(const std::vector<std::string>& names, 
                        const std::vector<T>& values, 
                        std::string_view qasm_inst_str)
{
    if (names.size() != values.size())
    {
        std::cerr << "_make_substitution_map: expected " << names.size() << " but only got "
                    << values.size() << ": " << qasm_inst_str << _die{};
    }

    std::unordered_map<std::string, T> subst_map;
    for (size_t i = 0; i < names.size(); i++)
        subst_map[names[i]] = values[i];
    return subst_map;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_parameter_substitution(EXPRESSION& param, const subst_map_type<EXPRESSION>& subst_map)
{
    for (auto& entry : param.terms)
    {
        for (auto& factor : entry.term.factors)
        {
            for (auto& val : factor.exponential_value.power_sequence)
            {
                if (std::holds_alternative<std::string>(val))
                {
                    auto it = subst_map.find(std::get<std::string>(val));
                    if (it != subst_map.end())
                        val.emplace<expr_ptr>(new EXPRESSION{it->second});
                }
            }
        }
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
_argument_substitution(prog::QASM_OPERAND& arg,
                        const subst_map_type<prog::QASM_OPERAND>& subst_map) 
{
    auto it = subst_map.find(arg.name);
    if (it != subst_map.end())
        arg = it->second;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class MAP_TYPE> void
_scan_and_die_on_conflict(const MAP_TYPE& x, const MAP_TYPE& y, std::string_view dupli_name)
{
    auto it = std::find_if(y.begin(), y.end(), [&x] (const auto& e) { return x.find(e.first) != x.end(); });
    if (it != y.end())
    {
        std::cerr << "_scan_and_die_on_conflict: duplicate " << dupli_name 
                    << " found during include: " << it->first << _die{};
    }
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

std::unordered_map<INSTRUCTION::TYPE, INSTRUCTION::TYPE>
_make_inverse_map()
{
    std::unordered_map<INSTRUCTION::TYPE, INSTRUCTION::TYPE> inv_map;
    inv_map.reserve(std::size(SELF_INVERSES) + 8);
    for (size_t i = 0; i < std::size(SELF_INVERSES); ++i)
        inv_map[SELF_INVERSES[i]] = SELF_INVERSES[i];

    auto add_rel = [&inv_map] (INSTRUCTION::TYPE a, INSTRUCTION::TYPE b)
                    {
                        inv_map[a] = b;
                        inv_map[b] = a;
                    };

    add_rel(INSTRUCTION::TYPE::S, INSTRUCTION::TYPE::SDG);
    add_rel(INSTRUCTION::TYPE::SX, INSTRUCTION::TYPE::SXDG);
    add_rel(INSTRUCTION::TYPE::T, INSTRUCTION::TYPE::TDG);

    return inv_map;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

size_t
_get_required_precision(const INSTRUCTION::fpa_type& angle)
{
    size_t msb = angle.join_word_and_bit_idx(angle.msb());
    if (msb == PROGRAM_INFO::fpa_type::NUM_BITS-1)
        msb = angle.join_word_and_bit_idx(fpa::negate(angle).msb());
    msb = PROGRAM_INFO::fpa_type::NUM_BITS-msb-1;

    return (msb/3) + 3;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

}  // anon

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////
