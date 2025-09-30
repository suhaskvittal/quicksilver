/*
    author: Suhas Vittal
    date:   22 August 2025
*/

#include "compiler/program.h"
#include "compiler/program/expression.h"
#include "compiler/program/oq2/lexer_wrapper.h"
#include "parser.tab.h"

#include <cstdio>
#include <deque>
#include <initializer_list>
#include <iomanip>
#include <iostream>

#include <zlib.h>

// `DROP_MEASUREMENT_GATES` is necessary for many QASMBench workloads, since they
// have invalid measurement syntax
#define DROP_MEASUREMENT_GATES
#define ALLOW_GATE_DECL_OVERRIDES
//#define PROGRAM_INFO_SHOW_GS_OUTPUT

//#define PROGRAM_INFO_VERBOSE

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

namespace prog
{

std::string
_generic_value_to_string(const EXPRESSION::generic_value_type& val)
{
    if (std::holds_alternative<int64_t>(val))
        return std::to_string(std::get<int64_t>(val));
    else if (std::holds_alternative<double>(val))
        return std::to_string(std::get<double>(val));
    else if (std::holds_alternative<std::string>(val))
        return std::get<std::string>(val);
    else
        return std::get<EXPRESSION::expr_ptr>(val)->to_string();
}

std::string 
EXPRESSION::to_string() const
{
    std::stringstream ss;
    for (size_t i = 0; i < termseq.size(); ++i)
    {
        // print out term operator:
        const auto& [term, op] = termseq[i];
        if (i > 0)
            ss << (op == OPERATOR::ADD ? " + " : " - ");

        ss << "(";
        for (size_t j = 0; j < term.size(); ++j)
        {
            const auto& [expval, op2] = term[j];
            if (j > 0)
                ss << (op2 == OPERATOR::MULTIPLY ? " * " : "/ ");

            if (expval.second)
                ss << "-";

            ss << "(";
            for (size_t k = 0; k < expval.first.size(); ++k)
            {
                if (k > 0)
                    ss << "^";
                ss << _generic_value_to_string(expval.first[k]);
            }
            ss << ")";
        }
        ss << ")";
    }

    return ss.str();
}

std::string
QASM_INST_INFO::to_string() const
{
    std::stringstream ss;

    std::stringstream gate_param_ss;

    gate_param_ss << gate_name;
    if (!params.empty())
    {
        gate_param_ss << "( ";
        for (size_t i = 0; i < params.size(); ++i)
        {
            if (i > 0)
                gate_param_ss << ", ";
            gate_param_ss << params[i].to_string();
        }
        gate_param_ss << " )";
    }

    ss << std::left << std::setw(24) << gate_param_ss.str();

    for (size_t i = 0; i < args.size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << args[i].name;
        if (args[i].index != operand_type::NO_INDEX)
            ss << "[" << args[i].index << "]";
    }

    return ss.str();
}

}   // namespace prog

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

using namespace prog;

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

    generate_calculated_stats();
}

void
PROGRAM_INFO::stats_type::generate_calculated_stats()
{
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO::PROGRAM_INFO(FILE* ostrm, ssize_t urot_precision)
    :ostrm_(ostrm),
    urot_precision_(urot_precision)
{}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

PROGRAM_INFO
PROGRAM_INFO::from_file(std::string input_file, ssize_t urot_precision)
{
    PROGRAM_INFO prog{nullptr, urot_precision};

    std::ifstream istrm(input_file);
    OQ2_LEXER lexer(istrm);
    yy::parser parser(lexer, prog, "");
    int retcode = parser();

    prog.final_stats_ = prog.analyze_program();

    return prog;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class WRITE_FUNC> void
_copy_data_from_file_to_file(FILE* istrm, const WRITE_FUNC& write_func)
{
    char buf[1024];

    int bytes_read;
    while ((bytes_read=fread(buf, sizeof(char), sizeof(buf), istrm)) > 0)
        write_func(buf, bytes_read);
}

PROGRAM_INFO::stats_type
PROGRAM_INFO::read_from_file_and_write_to_binary(std::string input_file, std::string output_file, size_t urot_precision)
{
    FILE* tmpstrm = tmpfile();

    PROGRAM_INFO prog(tmpstrm, urot_precision);

    // get the dirname of `input_file`
    std::string dirname = input_file.substr(0, input_file.find_last_of('/'));

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] reading file: " << input_file << ", new relative path: " << dirname << "\n";
#endif

    std::ifstream istrm(input_file);
    OQ2_LEXER lexer(istrm);
    yy::parser parser(lexer, prog, dirname);
    int retcode = parser();

    uint32_t num_qubits = prog.get_num_qubits();

    // need to get last set of stats and merge:
    prog.flush_and_clear_instructions();

    // rewind `tmpstrm` to start of file (now will use it for reading)
    rewind(tmpstrm);

    // create final output file:
    bool is_gz = output_file.find(".gz") != std::string::npos;
    if (is_gz)
    {
        gzFile ostrm = gzopen(output_file.c_str(), "w9");
        gzwrite(ostrm, &num_qubits, sizeof(num_qubits));
        _copy_data_from_file_to_file(tmpstrm, [ostrm] (void* buf, size_t size) { return gzwrite(ostrm, buf, size); });
        gzclose(ostrm);
    }
    else
    {
        FILE* ostrm = fopen(output_file.c_str(), "wb");
        fwrite(&num_qubits, sizeof(num_qubits), 1, ostrm);
        _copy_data_from_file_to_file(tmpstrm, [ostrm] (void* buf, size_t size) { return fwrite(buf, 1, size, ostrm); });
        fclose(ostrm);
    }

    // delete tmp file:
    fclose(tmpstrm);

    return prog.final_stats_;
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
        throw std::runtime_error("expected " + std::to_string(names.size()) + " but only got "
                                + std::to_string(values.size()) + ": " + std::string{qasm_inst_str});
    }

    std::unordered_map<std::string, T> subst_map;
    for (size_t i = 0; i < names.size(); ++i)
        subst_map[names[i]] = values[i];
    return subst_map;
}

void
_param_subst(EXPRESSION& param, const std::unordered_map<std::string, EXPRESSION>& subst_map)
{
    for (auto& [term, op1] : param.termseq)
    {
        for (auto& [expval, op2] : term)
        {
            for (auto& val : expval.first)
            {
                if (std::holds_alternative<std::string>(val))
                {
                    auto it = subst_map.find(std::get<std::string>(val));
                    if (it != subst_map.end())
                        val.emplace<EXPRESSION::expr_ptr>(new EXPRESSION{it->second});
                }
            }
        }
    }
}

void
_arg_subst(QASM_INST_INFO::operand_type& arg, 
            const std::unordered_map<std::string, QASM_INST_INFO::operand_type>& subst_map)
{
    auto it = subst_map.find(arg.name);
    if (it != subst_map.end())
        arg = it->second;
}

void 
PROGRAM_INFO::add_instruction(QASM_INST_INFO&& qasm_inst)
{
    // need to basis gate translation:
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] qasm_inst: " << qasm_inst.to_string() << "\n";
#endif

    // handle measure alias:
    if (qasm_inst.gate_name == "measure")
        qasm_inst.gate_name = "mz";

#if defined(DROP_MEASUREMENT_GATES)
    if (qasm_inst.gate_name == "mz")
        return;
#endif
    
    // treat barrier as a NOP:
    if (qasm_inst.gate_name == "barrier")
        return;

    auto basis_gate_it = std::find(std::begin(BASIS_GATES), std::end(BASIS_GATES), qasm_inst.gate_name);
    if (basis_gate_it != std::end(BASIS_GATES))
    {
        if (inst_read_ % 1'000'000 == 0)
            std::cout << "[ PROGRAM_INFO ] read " << inst_read_/1'000'000 << "M instructions\n";
        inst_read_++;

        INSTRUCTION::TYPE inst_type = static_cast<INSTRUCTION::TYPE>(
                                            std::distance(std::begin(BASIS_GATES), basis_gate_it));

        // Handle rotations
        fpa_type rotation{};
        std::vector<INSTRUCTION::TYPE> urotseq;
        if (inst_type == INSTRUCTION::TYPE::RX || inst_type == INSTRUCTION::TYPE::RZ)
        {
            // Given our basis gates, there can only be one parameter for rotation gates.
            rotation = expr::evaluate_expression(qasm_inst.params[0]).readout_fixed_point_angle();
            // ignore gates with an angle of 0:
            if (rotation.popcount() == 0)
                return;
            urotseq = unroll_rotation(rotation);
        }

        // first, check if any arguments are registers with width > 1 that are un-indexed:
        std::vector<bool> v_op_vec(qasm_inst.args.size(), false);
        std::vector<size_t> v_op_width(qasm_inst.args.size(), 0);
        for (size_t i = 0; i < qasm_inst.args.size(); ++i)
        {
            auto it = registers_.find(qasm_inst.args[i].name);
            if (it == registers_.end())
                throw std::runtime_error("register not found: " + qasm_inst.args[i].name);

            v_op_vec[i] = it->second.width > 1 && qasm_inst.args[i].index == QASM_INST_INFO::operand_type::NO_INDEX;
            v_op_width[i] = it->second.width;
        }

        // check if any vector operands are present:
        bool any_vector_operands = std::any_of(v_op_vec.begin(), v_op_vec.end(), [] (bool x) { return x; });
        if (any_vector_operands)
        {
            // compute vector operand width (if any vector operands violate this, we can raise an exception)
            auto v_op_it = std::find(v_op_vec.begin(), v_op_vec.end(), true);
            size_t first_vec_idx = std::distance(v_op_vec.begin(), v_op_it);
            size_t width = v_op_width[first_vec_idx];

#if defined(PROGRAM_INFO_VERBOSE)
            std::cout << "\tevaluated as vector instruction, expanded as:\n";
#endif

            // apply the operation multiple times:
            std::vector<qubit_type> qubits(qasm_inst.args.size());
            for (size_t i = 0; i < width; ++i)
            {
                // set all vector operands to use index `i`
                for (size_t j = 0; j < qasm_inst.args.size(); ++j)
                {
                    if (v_op_vec[j])
                    {
                        // first, make sure that the width is correct:
                        if (v_op_width[j] != width)
                        {
                            throw std::runtime_error("vector operand width mismatch (expected " + std::to_string(width) 
                                                    + " but got " + std::to_string(v_op_width[j]) 
                                                    + "): " + qasm_inst.args[j].name);
                        }

                        // now, set the qubit index:
                        qasm_inst.args[j].index = i;
                    }
                    qubits[j] = get_qubit_id_from_operand(qasm_inst.args[j]);
                }

                // create and push the instruction:
                INSTRUCTION inst{inst_type, qubits, rotation, urotseq.begin(), urotseq.end()};

#if defined(PROGRAM_INFO_VERBOSE)
                std::cout << "\t\t( " << i << " ) " << inst << "\n";
#endif

                instructions_.push_back(inst);
            }
        }
        else
        {
            // convert operands to qubits:
            std::vector<qubit_type> qubits(qasm_inst.args.size());
            std::transform(qasm_inst.args.begin(), qasm_inst.args.end(), qubits.begin(), 
                        [this] (const auto& x) { return this->get_qubit_id_from_operand(x); });

            INSTRUCTION inst{inst_type, qubits, rotation, urotseq.begin(), urotseq.end()};

#if defined(PROGRAM_INFO_VERBOSE)
            std::cout << "\tevaluated as: " << inst << "\n";
#endif

            instructions_.push_back(inst);
        }

        // check if `instructions_` is too large: if so, flush to output file:
        if (instructions_.size() >= MAX_INST_BEFORE_FLUSH && ostrm_ != nullptr)
            flush_and_clear_instructions();
    }
    else
    {
        // check for gate definition:
        auto gate_it = user_defined_gates_.find(qasm_inst.gate_name);
        if (gate_it == user_defined_gates_.end())
            throw std::runtime_error("gate not defined: " + qasm_inst.gate_name);

        // get gate definition:
        const auto& gate_def = gate_it->second;
        if (gate_def.body.empty())
            return;   // this is a NOP

        auto param_subst_map = _make_substitution_map(gate_def.params, qasm_inst.params, qasm_inst.gate_name);
        auto arg_subst_map = _make_substitution_map(gate_def.args, qasm_inst.args, qasm_inst.gate_name);

        for (const auto& q_inst : gate_def.body)
        {
            QASM_INST_INFO inst = q_inst;

            // parameter substitution:
            for (auto& p : inst.params)
                _param_subst(p, param_subst_map);

            for (auto& x : inst.args)
                _arg_subst(x, arg_subst_map);

            add_instruction(std::move(inst));
        }
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

void
PROGRAM_INFO::declare_gate(GATE_DEFINITION&& gate_def)
{
#if !defined(ALLOW_GATE_DECL_OVERRIDES)
    auto it = user_defined_gates_.find(gate_def.name);
    if (it != user_defined_gates_.end())
        throw std::runtime_error("gate already declared: " + gate_def.name);
#endif

    user_defined_gates_.insert({gate_def.name, std::move(gate_def)});
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class MAP_TYPE> void
_scan_and_die_on_conflict(const MAP_TYPE& x, const MAP_TYPE& y, std::string_view dupli_name)
{
    auto it = std::find_if(y.begin(), y.end(),
                            [&x] (const auto& e)
                            {
                                return x.find(e.first) != x.end();
                            });
    if (it != y.end())
        throw std::runtime_error("duplicate " + std::string{dupli_name} + " found during include: " + it->first);
}

void
PROGRAM_INFO::merge(PROGRAM_INFO&& other)
{
    // merge stats:
    other.final_stats_.merge(other.analyze_program());
    final_stats_.merge(other.final_stats_);

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
              std::make_move_iterator(other.instructions_.end()), std::back_inserter(instructions_));
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

void
PROGRAM_INFO::flush_and_clear_instructions()
{
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] flushing instructions to file\n";
#endif
    // first, do optimizations:
    [[ maybe_unused ]] size_t num_gates_removed = dead_gate_elimination();

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] done with optimizations, removed " << num_gates_removed << " gates\n";
#endif

    // update stats first while we have `instructions_`
    auto curr_stats = analyze_program();
    final_stats_.merge(curr_stats);

    // write to file:
    for (const auto& inst : instructions_)
    {
        if (inst.type == INSTRUCTION::TYPE::RZ || inst.type == INSTRUCTION::TYPE::RX)
        {
            if (inst.urotseq.empty()) // this is an RZ(0) or RZ(2*pi) that did not get caught -- just skip it:
                continue;
        }

        auto enc = inst.serialize();
        enc.read_write([this] (void* buf, size_t size) { return fwrite(buf, 1, size, this->ostrm_); });
    }

    // clear instructions:
    instructions_.clear();
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

qubit_type 
PROGRAM_INFO::get_qubit_id_from_operand(const QASM_INST_INFO::operand_type& operand) const
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
        throw std::runtime_error("operand index out of bounds: " 
                        + operand.name + "[" + std::to_string(operand.index) + "]");
    }

    return idx + std::max(operand.index, static_cast<ssize_t>(0));  // handle the case where the index is negative (NO_INDEX)
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

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

size_t
PROGRAM_INFO::dead_gate_elim_pass(size_t prev_gates_removed)
{
    size_t num_gates_before_opt = instructions_.size();

    // first pass: remove all rotation gates with an angle of 0:
    auto it = std::remove_if(instructions_.begin(), instructions_.end(),
                        [] (const auto& inst)
                        {
                            bool is_rot = inst.type == INSTRUCTION::TYPE::RX || inst.type == INSTRUCTION::TYPE::RZ;
                            return is_rot && inst.angle.popcount() == 0;
                        });
    instructions_.erase(it, instructions_.end());

    // second pass: remove any gates that cancel each other out. These are
    //   (1) self-inverses
    //   (2) gates with straightforward inverses (e.g., tdg + t, or rz(x) + rz(-x))
    //                    
    // Note that there is a common pattern: CX RZ(x) CX RZ(x) -- if x = 0, then we have removed,
    // the RZs, so we now have CX CX, which can be removed. So we first optimize for this:

    auto inv_map = _make_inverse_map();

    size_t i{1};
    while (i < instructions_.size())
    {
        auto& prev_inst = instructions_[i-1];
        auto& curr_inst = instructions_[i];
        if (curr_inst.type == INSTRUCTION::TYPE::RZ || curr_inst.type == INSTRUCTION::TYPE::RX)
        {
            // then, we need just need to check that the previous gate is also an RZ/RX but also
            // has the complementary angle:
            if (prev_inst.type == curr_inst.type)
            {
                auto angle_sum = fpa::add(curr_inst.angle, prev_inst.angle);
                if (angle_sum.popcount() == 0)
                {
                    prev_inst.type = INSTRUCTION::TYPE::NIL;
                    curr_inst.type = INSTRUCTION::TYPE::NIL;
                    i += 2;  // since `instructions_[i]` was removed, jump 2 instructions ahead
                    continue;
                }
            }
        }

        auto it = inv_map.find(curr_inst.type);
        if (it != inv_map.end() && prev_inst.type == it->second)
        {
            // now, check that the args are the same:
            bool same{true};
            for (size_t j = 0; j < curr_inst.qubits.size(); ++j)
                same &= (curr_inst.qubits[j] == prev_inst.qubits[j]);

            if (same)
            {
                prev_inst.type = INSTRUCTION::TYPE::NIL;
                curr_inst.type = INSTRUCTION::TYPE::NIL;
                i += 2;  // since `instructions_[i]` was removed, jump 2 instructions ahead
                continue;
            }
        }
        i++;
    }

    it = std::remove_if(instructions_.begin(), instructions_.end(),
                    [] (const auto& inst) { return inst.type == INSTRUCTION::TYPE::NIL; });
    instructions_.erase(it, instructions_.end());

    size_t removed_this_pass = num_gates_before_opt - instructions_.size();
    if (removed_this_pass == 0)
        return prev_gates_removed;
    else
        return dead_gate_elim_pass(prev_gates_removed + removed_this_pass);
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

template <class T> double
_mean(T x, T y)
{
    return static_cast<double>(x) / static_cast<double>(y);
}

PROGRAM_INFO::stats_type
PROGRAM_INFO::analyze_program() const
{
    stats_type out{};

    // first, we need to reorganize the instructions into layers:
    iterate_through_instructions_by_layer(
        [&out] (size_t layer_id, std::vector<const INSTRUCTION*> layer)
        {
            for (const auto* inst : layer)
            {
                bool is_sw_gate = (inst->type == INSTRUCTION::TYPE::X 
                                    || inst->type == INSTRUCTION::TYPE::Y 
                                    || inst->type == INSTRUCTION::TYPE::Z);
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
                if (is_rot)
                    out.unrolled_inst_count += inst->urotseq.size();
                else
                    out.unrolled_inst_count++;
            }
        }
    );

    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////

// declared some constexpr `INSTRUCTION::TYPE`s to make the code more readable:
constexpr static INSTRUCTION::TYPE H = INSTRUCTION::TYPE::H;
constexpr static INSTRUCTION::TYPE X = INSTRUCTION::TYPE::X;
constexpr static INSTRUCTION::TYPE Z = INSTRUCTION::TYPE::Z;
constexpr static INSTRUCTION::TYPE S = INSTRUCTION::TYPE::S;
constexpr static INSTRUCTION::TYPE SDG = INSTRUCTION::TYPE::SDG;
constexpr static INSTRUCTION::TYPE T = INSTRUCTION::TYPE::T;
constexpr static INSTRUCTION::TYPE TDG = INSTRUCTION::TYPE::TDG;
constexpr static INSTRUCTION::TYPE SX = INSTRUCTION::TYPE::SX;
constexpr static INSTRUCTION::TYPE SXDG = INSTRUCTION::TYPE::SXDG;
constexpr static INSTRUCTION::TYPE TX = INSTRUCTION::TYPE::TX;
constexpr static INSTRUCTION::TYPE TXDG = INSTRUCTION::TYPE::TXDG;

template <class ITERABLE> std::string
_urotseq_to_string(ITERABLE iterable)
{
    std::stringstream strm;
    bool first{true};
    for (auto x : iterable)
    {
        if (!first)
            strm << "'";
        first = false;
        std::string_view sx = BASIS_GATES[static_cast<size_t>(x)];
        strm << sx;
    }
    return strm.str();
}

void
_replace_subsequence(std::vector<INSTRUCTION::TYPE>& urotseq, 
                        std::initializer_list<INSTRUCTION::TYPE> subseq, 
                        INSTRUCTION::TYPE replacement)
{
#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "\t\turotseq: " << _urotseq_to_string(urotseq)
                    << ", replace: " << _urotseq_to_string(subseq)
                    << " -> " << BASIS_GATES[static_cast<size_t>(replacement)] << "\n";
#endif

    size_t width = subseq.size();
    std::deque<INSTRUCTION::TYPE> buf;
    for (size_t i = 0; i < urotseq.size(); i++)
    {
        if (buf.size() == width)
            buf.pop_front();
        buf.push_back(urotseq[i]);

        bool match = buf.size() == width && std::equal(buf.begin(), buf.end(), subseq.begin());
        if (match)
        {
            // set `i-width+1` to `replacement
            urotseq[i-width+1] = replacement;
            // delete `i-width+2` through `i`
            for (size_t j = i-width+2; j <= i; j++)
                urotseq[j] = INSTRUCTION::TYPE::NIL;
            buf.clear();
        }
    }

    // finally, remove any `NIL` gates:
    auto it = std::remove_if(urotseq.begin(), urotseq.end(),
                        [] (const auto& inst) { return inst == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());
}

void
_flip_h_subsequence(std::vector<INSTRUCTION::TYPE>& urotseq)
{
    // find the largest such subsequence sandwiched by `H` and replace those gates with the X/Z rotation alternative
    std::unordered_map<INSTRUCTION::TYPE, INSTRUCTION::TYPE> xz_map
    {
        {X, Z}, {Z, X},
        {S, SX}, {SX, S},
        {SDG, SXDG}, {SXDG, SDG},
        {T, TX}, {TX, T},
        {TXDG, TDG}, {TDG, TXDG}
    };

    // find the first two H gates:
    auto h_max_begin = std::find(urotseq.begin(), urotseq.end(), H);
    if (h_max_begin == urotseq.end())
        return;

    auto h_max_end = std::find(h_max_begin+1, urotseq.end(), H);
    if (h_max_end == urotseq.end())
        return;

    size_t max_len = std::distance(h_max_begin, h_max_end);

    // now search for a better subsequence:
    auto h_begin = std::find(h_max_end+1, urotseq.end(), H);
    while (h_begin != urotseq.end())
    {
        auto h_end = std::find(h_begin+1, urotseq.end(), H);
        if (h_end == urotseq.end())
            break;

        size_t len = std::distance(h_begin, h_end);
        if (len > max_len)
        {
            max_len = len;
            h_max_begin = h_begin;
            h_max_end = h_end;
        }

        // recompute `h_begin` and try again:
        h_begin = std::find(h_end+1, urotseq.end(), H);
    }

    // now, we should have the best subsequence: flip all gates in between:
    for (auto it = h_max_begin+1; it != h_max_end; it++)
        *it = xz_map[*it];

    // set the H gates to nil and remove them:
    *h_max_begin = INSTRUCTION::TYPE::NIL;
    *h_max_end = INSTRUCTION::TYPE::NIL;
    auto it = std::remove_if(urotseq.begin(), urotseq.end(), [] (const auto& inst) { return inst == INSTRUCTION::TYPE::NIL; });
    urotseq.erase(it, urotseq.end());

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "\t\turotseq: " << _urotseq_to_string(urotseq) << "\n";
#endif
}

std::vector<INSTRUCTION::TYPE>
_gs_cli_call(PROGRAM_INFO::fpa_type rotation, ssize_t urot_precision)
{
    std::stringstream cli_strm;
    // determine the CLI arguments for gridsynth:
    constexpr size_t ROTW = PROGRAM_INFO::fpa_type::NUM_BITS;
    size_t precision = urot_precision == PROGRAM_INFO::USE_MSB_TO_DETERMINE_UROT_PRECISION
                        ? ROTW - rotation.join_word_and_bit_idx(rotation.msb()) + 8
                        : urot_precision;

    cli_strm << "gridsynth -b " << precision << " -p \"" << fpa::to_string(rotation, true) << "\"";
    std::string cmd = cli_strm.str();
    
    std::vector<INSTRUCTION::TYPE> out{};
    out.reserve(2*precision);

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] running gridsynth with command: `" << cmd << "`"
            << "\n\t\tgridsynth output: ";
#elif defined(PROGRAM_INFO_SHOW_GS_OUTPUT)
    std::cout << "[ PROGRAM_INFO ] running gridsynth for angle: " << fpa::to_string(rotation) 
                                                                    << ", hex string: " << rotation.to_hex_string()
                                                                    << ", precision: " << precision << "\n";
#endif
    
    // run gridsynth and read its output:
    FILE* gs = popen(cmd.c_str(), "r");
    char c;
    while ((c=fgetc(gs)) != EOF)
    {
#if defined(PROGRAM_INFO_VERBOSE)
        std::cout << c;
#endif
        if (c == 'H')
            out.push_back(H);
        else if (c == 'T')
            out.push_back(T);
        else if (c == 'X')
            out.push_back(X);
        else if (c == 'Z')
            out.push_back(Z);
        else if (c == 'S')
            out.push_back(S);
    }

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "\n";
#endif

    pclose(gs);

    // now we need to coalesce gates inplace:

    [[maybe_unused]] size_t urotseq_original_size = out.size();

    // first, convert all SS -> Z
    _replace_subsequence(out, {S, S}, Z);
    // then, convert all SZ and ZS -> SDG
    _replace_subsequence(out, {S, Z}, SDG);
    _replace_subsequence(out, {Z, S}, SDG);
    // then, convert all T*SDG and SDG*T -> TDG
    _replace_subsequence(out, {T, SDG}, TDG);
    _replace_subsequence(out, {SDG, T}, TDG);
    // then, convert all H*S*H -> S and H*SDG*H -> SXDG
    _replace_subsequence(out, {H, S, H}, S);
    _replace_subsequence(out, {H, SDG, H}, SXDG);
    // then, convert all H*TX*H -> TX and H*TXDG*H -> TXDG
    _replace_subsequence(out, {H, T, H}, TX);
    _replace_subsequence(out, {H, TDG, H}, TXDG);

    // finally, flip any subsequences sandwiched by `H` gates:
    size_t h_count = std::count(out.begin(), out.end(), H);
    while (h_count > 1)
    {
        _flip_h_subsequence(out);
        h_count -= 2;
    }
    [[maybe_unused]] size_t urotseq_reduced_size = out.size();

#if defined(PROGRAM_INFO_VERBOSE)
    std::cout << "[ PROGRAM_INFO ] reduced urotseq size from " << urotseq_original_size << " to " << urotseq_reduced_size << "\n";
#elif defined(PROGRAM_INFO_SHOW_GS_OUTPUT)
    std::cout << "\tgs gate count (post opt) = " << urotseq_reduced_size << "\n";
#endif

    if (urotseq_reduced_size == 0)
        throw std::runtime_error("gridsynth returned an empty urotseq, original size: " + std::to_string(urotseq_original_size));

    return out;
}

std::vector<INSTRUCTION::TYPE>
PROGRAM_INFO::unroll_rotation(fpa_type rotation)
{
    auto it = rotation_cache_.find(rotation);
    if (it != rotation_cache_.end())
        return it->second;

    std::vector<INSTRUCTION::TYPE> out = _gs_cli_call(rotation, urot_precision_);
    rotation_cache_.insert({rotation, out});
    return out;
}

////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////