# author: Suhas Vittal
# date: 20 July 2026

from common import *
import random
import math
import os

#################################################################
#################################################################

# residue-system primes (1024 distinct 21-bit primes for RSA-2048) live in residue_primes_21.txt

#################################################################
#################################################################

def _table_lookup_unary_iteration_helper(q_index: str,
                                         index_lb: int,
                                         q_output: str,
                                         unary_it_anc: str,
                                         depth: int,
                                         max_depth: int,
                                         c: str,
                                         lookup_table_bit_width: int
):
    '''
        `q_index`: indexing quantum register. Indexing begins from `index_lb`
        `q_output`: output register for lookup
        `unary_it_anc`: ancilla qubits for unary iteration
        `depth`: current depth of unary iteration
        `max_depth`: max depth of unary iteration (when lookup finally occurs)
        `c`: control qubit for unary iteration
        `lookup_table_bit_width`: size of integers in classical table
    '''
    out = ''
    if depth == max_depth:
        # I'll be honest, I don't really care about the actual table value. So, I am generating
        # some random value for the table value we are implementing
        x = random.randint(0, 2**lookup_table_bit_width)
        for i in range(0, lookup_table_bit_width):
            if (x & (1<<i)) > 0:
                out += str(GATE('cx').operand(c).operand(q_output, i))
    else:
        out += str(GATE('x').operand(q_index, index_lb+depth)) \
                    + str(GATE('ccx').operand(c).operand(q_index, index_lb+depth).operand(unary_it_anc, depth))
        out += _table_lookup_unary_iteration_helper(q_index,
                                                     index_lb,
                                                     q_output,
                                                     unary_it_anc,
                                                     depth+1,
                                                     max_depth,
                                                     f'{unary_it_anc}[{depth}]',
                                                     lookup_table_bit_width)
        out += str(GATE('x').operand(q_index, index_lb+depth)) \
                    + str(GATE('cx').operand(c).operand(unary_it_anc, depth))
        out += _table_lookup_unary_iteration_helper(q_index,
                                                     index_lb,
                                                     q_output,
                                                     unary_it_anc,
                                                     depth+1,
                                                     max_depth,
                                                     f'{unary_it_anc}[{depth}]',
                                                     lookup_table_bit_width)
        out += str(GATE('mx').operand(unary_it_anc, depth))
    return out


#################################################################
#################################################################

def _loop1_unary_iteration(q_exponent: str,
                           idx_lb: int,
                           idx_ub: int,
                           q_dlog_delta: str,
                           q_dlog_w: int,
                           unary_it_anc: str,
                           address_bits: int):
    out = ''
    out += str(GATE('x').operand(q_exponent, idx_lb))
    out += _table_lookup_unary_iteration_helper(q_exponent,
                                                idx_lb,
                                                q_dlog_delta,
                                                unary_it_anc,
                                                1,
                                                address_bits,
                                                f'{q_exponent}[{idx_lb}]',
                                                q_dlog_w)
    out += str(GATE('x').operand(q_exponent, idx_lb))
    out += _table_lookup_unary_iteration_helper(q_exponent,
                                                idx_lb,
                                                q_dlog_delta,
                                                unary_it_anc,
                                                1,
                                                address_bits,
                                                f'{q_exponent}[{idx_lb}]',
                                                q_dlog_w)
    return out

def _loop1_adder(q_dlog: str,
                 q_dlog_partial_sum: str,
                 q_dlog_delta: str,
                 q_dlog_w: int
):
    out = ''
    # compute `q_dlog[i]` (sum)
    for i in range(q_dlog_w):
        # TODO: we need to support PPMs for this. CX is very very suboptimal here
        if i == 0:
            out += str(GATE('cx').operand(q_dlog_partial_sum, 0).operand(q_dlog, 0)) \
                    + str(GATE('cx').operand(q_dlog_delta, 0).operand(q_dlog, 0))
        else:
            # XOR both addends with previous sum qubit ( ai + si ) * ( bi + si )
            for qr in [q_dlog_partial_sum, q_dlog_delta]:
                out += str(GATE('cx').operand(q_dlog, i-1).operand(qr, i-1))

            # do AND between both addends (product in previous comment)
            out += str(GATE('ccx').operand(q_dlog_partial_sum, i-1).operand(q_dlog_delta, i-1).operand(q_dlog, i))

            # two steps: we need to undo XOR between addends and previous sum qubit and also XOR with
            # current sum qubit
            for qr in [q_dlog_partial_sum, q_dlog_delta]:
                # undo 
                out += str(GATE('cx').operand(q_dlog, i-1).operand(qr, i-1))
                # XOR
                for j in [i-1, i]:
                    out += str(GATE('cx').operand(qr, j).operand(q_dlog, i))
            # XOR previous sum qubit with new sum qubit
            out += str(GATE('cx').operand(q_dlog, i-1).operand(q_dlog, i))

    # uncompute partial sum
    for i in range(q_dlog_w):
        out += str(GATE('mx').operand(q_dlog_partial_sum, i))
    return out


def loop1(q_dlog: str, 
          q_dlog_partial_sum: str,
          q_dlog_delta: str, 
          q_dlog_w: int, 
          q_exponent: str, 
          unary_it_anc: str,
          # loop variables:
          w1: int,
          m: int
):
    '''
        Implements the lookup + add in loop1. 
            `q_dlog` is the register for the discrete log
            `q_dlog_partial_sum` is the partial sum for the discrete log
            `q_dlog_delta` is the register for the table lookup outcome
            `q_exponent` is the superposed exponent to do modular exponentiation with.
                used to index stuff
            `unary_it_anc` is the ancilla register for unary iteration

            `w1` and `m` are the corresponding variables in Gidney's work
    '''
    
    out = ''
    num_windows = int(math.ceil(m/w1))
    for j in range(num_windows):
        address_bits = min(w1, m-j*w1)

        # if `j > 0`, then swap `q_dlog` with `q_dlog_partial_sum`. This is just for bookkeeping
        if j > 0:
            out += str(GATE('swap').operand(q_dlog).operand(q_dlog_partial_sum))

        # compute lower bound and upper bound of slice for `q_exponent`
        lb = j*w1
        ub = lb + address_bits

        # step 1: load table value into `q_dlog_delta`
        out += _loop1_unary_iteration(q_exponent, lb, ub, q_dlog_delta, q_dlog_w, unary_it_anc, address_bits)
        # step 2: add `q_dlog_delta` to `q_dlog_partial_sum`
        out += _loop1_adder(q_dlog, q_dlog_partial_sum, q_dlog_delta, q_dlog_w)
    return out

#################################################################
#################################################################

def _q(reg: str, idx: int) -> str:
    return f'{reg}[{idx}]'

def _g(name: str, *qubits: str) -> str:
    '''build a gate string from a name and already-indexed qubit operands (e.g. `dlog[3]`)'''
    g = GATE(name)
    for q in qubits:
        g.operand(q)
    return str(g)

def _ripple_carry_const_adder(q_target: str,
                              lo: int,
                              width: int,
                              constant: int,
                              carry_anc: str,
                              ctrl: str = None,
                              sub: bool = False) -> str:
    '''
        Emits |a> --> |(a + constant) mod 2^width> on the slice `q_target[lo : lo+width]`.

        This is a Toffoli ripple-carry adder against a *classical* `constant` (VBE-style:
        compute all carries forward, write the MSB sum, then uncompute each carry while
        writing its sum bit -- the reverse pass reuses the still-intact low bits, so the
        carry ancillas end clean).

            `carry_anc`: ancilla register holding carries. Carry *into* bit `i` lives in
                `carry_anc[i]` (bit 0 has no incoming carry). Needs `width` qubits.
            `ctrl`: if set (an already-indexed qubit string), the whole addition is
                controlled on it -- i.e. add `constant` iff `ctrl` is set. The control's
                GHZ fanout into per-bit copies is implicit/free (a single lattice-surgery
                layer, uncounted), so no fanout/prep gates are emitted.
            `sub`: if True, subtract instead (add the two's-complement constant). Working
                mod 2^width means the top qubit captures the borrow/sign bit.

        TODO: like `_loop1_adder`, the carry chain should be lowered to PPMs; CX/CCX is
        suboptimal here.
    '''
    mask = (1 << width) - 1
    constant &= mask
    if sub:
        constant = (-constant) & mask

    def carry_ops(i: int) -> list:
        '''gates that compute the carry out of bit `i` into `carry_anc[i+1]`'''
        ci = (constant >> i) & 1
        a = _q(q_target, lo+i)
        cout = _q(carry_anc, i+1)
        ops = []
        if i == 0:
            # no incoming carry; carry_out = a AND (ci [AND ctrl])
            if ci == 1:
                if ctrl is None:
                    ops.append(_g('cx', a, cout))
                else:
                    ops.append(_g('ccx', a, ctrl, cout))
        else:
            cin = _q(carry_anc, i)
            if ci == 0:
                # majority(a, 0, cin) = a AND cin
                ops.append(_g('ccx', a, cin, cout))
            elif ctrl is None:
                # majority(a, 1, cin) = a OR cin
                ops.append(_g('cx', a, cout))
                ops.append(_g('cx', cin, cout))
                ops.append(_g('ccx', a, cin, cout))
            else:
                # majority(a, ctrl, cin)
                ops.append(_g('ccx', a, ctrl, cout))
                ops.append(_g('ccx', ctrl, cin, cout))
                ops.append(_g('ccx', a, cin, cout))
        return ops

    def sum_ops(i: int) -> list:
        '''gates that overwrite bit `i` with sum = a XOR (ci [AND ctrl]) XOR carry_in'''
        ci = (constant >> i) & 1
        a = _q(q_target, lo+i)
        ops = []
        if i >= 1:
            ops.append(_g('cx', _q(carry_anc, i), a))
        if ci == 1:
            if ctrl is None:
                ops.append(_g('x', a))
            else:
                ops.append(_g('cx', ctrl, a))
        return ops

    out = ''
    # forward: compute carries into carry_anc[1 .. width-1]
    for i in range(width-1):
        out += ''.join(carry_ops(i))
    # most-significant sum bit (its carry is never consumed further)
    out += ''.join(sum_ops(width-1))
    # backward: uncompute each carry, then write its sum bit
    for i in range(width-2, -1, -1):
        out += ''.join(reversed(carry_ops(i)))
        out += ''.join(sum_ops(i))
    return out


def loop2(q_target: str,
          q_target_w: int,
          modulus: int,
          compressed_len: int,
          carry_anc: str) -> str:
    '''
        Compresses `q_target % modulus` into the low `compressed_len` qubits of `q_target`
        via restoring binary long division (Gidney 2025, Appendix A.1 `loop2`).

            `q_target`: register holding the remainder to compress (width `q_target_w`).
            `modulus`: the modulus of the remainder computation.
            `compressed_len`: target width of the remainder; at least `modulus.bit_length()`.
            `carry_anc`: ancilla register for the ripple-carry adders (width `q_target_w`).

        Precondition (guaranteed by construction in the real algorithm): the accumulator must
        be wide enough that its value is `< modulus << (q_target_w - compressed_len)`, i.e.
        the quotient bits fit above the compressed remainder. Given that, the low
        `compressed_len` qubits end holding `value % modulus`.

        Each division step is two constant additions (Table 3): a subtraction of the
        `threshold` that also lands the sign/borrow bit in `q_target[n]`, and an add-back of
        `threshold` into the low `n` bits controlled on that sign bit (a GHZ lookup). The
        compressed remainder is left in `q_target[0:compressed_len]`; the high quotient bits
        are cleaned up later by `unloop2` (out of scope). Emission is in-place; there is no
        QASM analogue of the reference code's returned slice.
    '''
    out = ''
    n = q_target_w
    while n > compressed_len:
        n -= 1
        threshold = modulus << (n - compressed_len)
        # step 1: Q_target[:n+1] -= threshold  (sign/borrow bit ends up in q_target[n])
        out += _ripple_carry_const_adder(q_target, 0, n+1, threshold, carry_anc, sub=True)
        # step 2: Q_target[:n] += threshold, controlled on the sign bit q_target[n] (GHZ lookup)
        out += _ripple_carry_const_adder(q_target, 0, n, threshold, carry_anc,
                                         ctrl=_q(q_target, n))
    return out

#################################################################
#################################################################

def _loop3_lookup_helper(addr_qubits: list,
                         q_output: str,
                         out_width: int,
                         unary_it_anc: str,
                         depth: int,
                         c: str) -> str:
    '''
        Unary-iteration recursion over an ordered list of address qubits. Same structure as
        `_table_lookup_unary_iteration_helper`, but the address is an explicit qubit list (so
        it may span multiple registers) instead of a contiguous slice.
    '''
    out = ''
    if depth == len(addr_qubits):
        # leaf: XOR a random `out_width`-bit table value into `q_output`, controlled on `c`
        x = random.randint(0, (1 << out_width) - 1)
        for i in range(out_width):
            if (x >> i) & 1:
                out += _g('cx', c, _q(q_output, i))
    else:
        b = addr_qubits[depth]
        anc = _q(unary_it_anc, depth)
        out += _g('x', b) + _g('ccx', c, b, anc)
        out += _loop3_lookup_helper(addr_qubits, q_output, out_width, unary_it_anc, depth+1, anc)
        out += _g('x', b) + _g('cx', c, anc)
        out += _loop3_lookup_helper(addr_qubits, q_output, out_width, unary_it_anc, depth+1, anc)
        out += _g('mx', anc)
    return out

def _loop3_lookup(addr_qubits: list,
                  q_output: str,
                  out_width: int,
                  unary_it_anc: str) -> str:
    '''
        `q_output ^= table[addr_qubits]`: a unary-iteration QROM lookup that XORs a random
        `out_width`-bit table entry (selected by the address qubits) into `q_output`.
        `addr_qubits` is an ordered list of already-indexed qubit strings (LSB first), so the
        address can be a concatenation of windows from different registers.
    '''
    if len(addr_qubits) == 0:
        # degenerate single-entry table: unconditional XOR
        x = random.randint(0, (1 << out_width) - 1)
        return ''.join(_g('x', _q(q_output, i)) for i in range(out_width) if (x >> i) & 1)
    # seed on the first address bit (its two values are the two top-level branches)
    seed = addr_qubits[0]
    out = _g('x', seed)
    out += _loop3_lookup_helper(addr_qubits, q_output, out_width, unary_it_anc, 1, seed)
    out += _g('x', seed)
    out += _loop3_lookup_helper(addr_qubits, q_output, out_width, unary_it_anc, 1, seed)
    return out


def _ripple_carry_qq_adder(a_reg: str,
                           a_lo: int,
                           a_width: int,
                           b_reg: str,
                           b_lo: int,
                           b_width: int,
                           carry_anc: str,
                           sub: bool = False) -> str:
    '''
        Quantum-quantum ripple-carry adder: `a[a_lo:a_lo+a_width] += b[b_lo:b_lo+b_width]` mod
        2^a_width, with `a_width >= b_width` (b's missing high bits are treated as 0). Same
        VBE / carry-ancilla structure as `_ripple_carry_const_adder`, but the addend bits are
        qubits. `sub=True` emits the reverse gate list, i.e. `a -= b` (the adder is a unitary
        whose inverse is subtraction; every gate is self-inverse).

            `carry_anc`: ancilla register for carries; needs `a_width` qubits.

        TODO: as elsewhere, the carry chain should be lowered to PPMs.
    '''
    def bbit(i):
        return _q(b_reg, b_lo+i) if i < b_width else None

    def carry_ops(i: int) -> list:
        a = _q(a_reg, a_lo+i)
        cout = _q(carry_anc, i+1)
        b = bbit(i)
        ops = []
        if i == 0:
            if b is not None:
                ops.append(_g('ccx', a, b, cout))       # majority(a, b, 0) = a AND b
        else:
            cin = _q(carry_anc, i)
            if b is None:
                ops.append(_g('ccx', a, cin, cout))     # majority(a, 0, cin) = a AND cin
            else:
                ops.append(_g('ccx', a, b, cout))       # majority(a, b, cin)
                ops.append(_g('ccx', b, cin, cout))
                ops.append(_g('ccx', a, cin, cout))
        return ops

    def sum_ops(i: int) -> list:
        a = _q(a_reg, a_lo+i)
        b = bbit(i)
        ops = []
        if i >= 1:
            ops.append(_g('cx', _q(carry_anc, i), a))
        if b is not None:
            ops.append(_g('cx', b, a))
        return ops

    ops = []
    for i in range(a_width-1):
        ops += carry_ops(i)
    ops += sum_ops(a_width-1)
    for i in range(a_width-2, -1, -1):
        ops += list(reversed(carry_ops(i)))
        ops += sum_ops(i)
    if sub:
        ops = list(reversed(ops))
    return ''.join(ops)


def loop3(q_dlog: str,
          q_dlog_w: int,
          q_result: str,
          q_helper: str,
          modulus: int,
          window3a: int,
          window3b: int,
          lookup_out: str,
          unary_it_anc: str,
          carry_anc: str) -> str:
    '''
        Computes `pow(generator, q_dlog, modulus)` into `q_result` via windowed modular
        multiplication (Gidney 2025, Appendix A.1 `loop3`). Forward direction only.

            `q_dlog`: superposed exponent (the compressed dlog), width `q_dlog_w`.
            `q_result`, `q_helper`: the two working registers, each width `modulus.bit_length()+1`
                (the extra top qubit is the modular-reduction wrap/sign bit).
            `modulus`: the prime `p` for this residue.
            `window3a`, `window3b`: exponent- and residue-window sizes (w3a, w3b).
            `lookup_out`: temp register for QROM lookup outputs, width `modulus.bit_length()`.
            `unary_it_anc`: ancilla for unary iteration (width >= max address length).
            `carry_anc`: ancilla for the ripple-carry adders (width `modulus.bit_length()+1`).

        Each body step is a QROM lookup + a modular subtraction (a quantum-quantum subtraction
        plus a `ghz_lookup(modulus)` add-back on the wrap bit). The wrap bit and lookup outputs
        are cleared by measurement-based uncompute (`mx`); the resulting phase corrections are
        DEFERRED to `unloop3` (not emitted here). The result deterministically ends in `q_result`
        (the emitted swaps physically move the product), and `q_helper` is cleared; emission is
        in-place so there is no QASM return value.

        NOTE: the `q_result`/`q_helper` swap is emitted as a register-level `swap` (matching
        loop1). On a fault-tolerant machine a swap is just a relabel, so it is effectively free.
    '''
    out = ''
    mod_w = modulus.bit_length()
    reg_w = mod_w + 1
    num_windows3a = int(math.ceil(q_dlog_w / window3a))
    num_windows3b = int(math.ceil(mod_w / window3b))

    # init: skip the first 2 exponent windows with a direct XOR-lookup into q_result
    init_bits = min(2*window3a, q_dlog_w)
    init_addr = [_q(q_dlog, b) for b in range(init_bits)]
    out += _loop3_lookup(init_addr, q_result, mod_w, unary_it_anc)

    # windowed modular multiplication (the running product lives in q_result; each window builds
    # the next product in q_helper, then a swap moves it back into q_result)
    for j in range(2, num_windows3a):
        expo_lb = j*window3a
        expo_addr = [_q(q_dlog, expo_lb+b) for b in range(window3a) if expo_lb+b < q_dlog_w]
        for k in range(num_windows3b):
            res_lb = k*window3b
            res_addr = [_q(q_result, res_lb+b) for b in range(window3b) if res_lb+b < mod_w]
            # Q_l = (Q_l1 << w3b) | Q_l0 : residue window (low) concatenated with exponent window (high)
            addr = res_addr + expo_addr
            # 1) QROM lookup table[addr] into lookup_out
            out += _loop3_lookup(addr, lookup_out, mod_w, unary_it_anc)
            # 2) modular subtraction: q_helper -= lookup_out (borrow lands in the wrap bit)
            out += _ripple_carry_qq_adder(q_helper, 0, reg_w, lookup_out, 0, mod_w, carry_anc, sub=True)
            # 3) mod-fix add-back: q_helper[:mod_w] += modulus, controlled on the wrap bit
            out += _ripple_carry_const_adder(q_helper, 0, mod_w, modulus, carry_anc,
                                             ctrl=_q(q_helper, mod_w))
            # 4) measure-clear the wrap bit (phaseup deferred to unloop3)
            out += _g('mx', _q(q_helper, mod_w))
            # 5) measurement-based uncompute of the lookup output (phaseups deferred)
            for b in range(mod_w):
                out += _g('mx', _q(lookup_out, b))
        # swap the result/helper roles (register-level SWAP; a free relabel on FT hardware)
        out += _g('swap', q_result, q_helper)

    # cleanup: measurement-based deallocation of the leftover helper (deferred)
    for b in range(reg_w):
        out += _g('mx', _q(q_helper, b))
    return out

#################################################################
#################################################################

def _phaseup(addr_qubits: list,
             unary_it_anc: str,
             depth: int = None,
             c: str = None) -> str:
    '''
        Phase-flip lookup modeling `qpu.z(table[addr])`: applies `(-1)^table[addr]` for a random
        boolean table addressed by `addr_qubits` (an ordered list of already-indexed qubit
        strings). Same unary-iteration structure as `_loop3_lookup`, but the leaf emits a `z`
        on the selected path control (with a random per-leaf phase bit) instead of a `cx` into
        an output register. This is Gidney's `phaseup` -- it resolves a lookup's vent.
    '''
    if depth is None:
        # top-level: seed on the first address bit (mirror `_loop3_lookup`)
        if len(addr_qubits) == 0:
            return ''  # zero-bit address: a global phase, not representable / no-op
        seed = addr_qubits[0]
        out = _g('x', seed)
        out += _phaseup(addr_qubits, unary_it_anc, 1, seed)
        out += _g('x', seed)
        out += _phaseup(addr_qubits, unary_it_anc, 1, seed)
        return out
    out = ''
    if depth == len(addr_qubits):
        # leaf: phase-flip the selected branch with probability 1/2 (random vent bit)
        if random.randint(0, 1):
            out += _g('z', c)
    else:
        b = addr_qubits[depth]
        anc = _q(unary_it_anc, depth)
        out += _g('x', b) + _g('ccx', c, b, anc)
        out += _phaseup(addr_qubits, unary_it_anc, depth+1, anc)
        out += _g('x', b) + _g('cx', c, anc)
        out += _phaseup(addr_qubits, unary_it_anc, depth+1, anc)
        out += _g('mx', anc)
    return out


def _ripple_carry_compare(a_reg: str,
                          a_lo: int,
                          a_width: int,
                          b_reg: str,
                          b_lo: int,
                          b_width: int,
                          carry_anc: str,
                          cmp_anc: str,
                          less_than: bool = False) -> str:
    '''
        Phase-flip comparison `qpu.z(a >= b)`: emits the ripple carry chain of `a - b` (the same
        VBE carry structure as the adders, but with no sum writes), applies `z cmp_anc` on the
        carry-out predicate (`a >= b` iff the subtraction produced no final borrow), then
        uncomputes the carry chain. `a` and `b` are preserved and all ancillas end clean.

        If `less_than` is set, phase-flip on `a < b` instead (the complement of the carry-out
        predicate), realized by wrapping the `z` with an `x` on `cmp_anc`.

        `a - b` is `a + (~b) + 1`; the final carry-out (carry into bit `a_width`) is 1 exactly
        when `a >= b`. We ripple that carry into `carry_anc[1 .. a_width]` with the constant `+1`
        folded into bit 0's carry-in, then read `carry_anc[a_width]`.

            `carry_anc`: needs `a_width+1` qubits.  `cmp_anc`: single qubit for the predicate.
    '''
    def bbit(i):
        return _q(b_reg, b_lo+i) if i < b_width else None

    def carry_ops(i: int) -> list:
        # carry_out(i) = majority(a_i, (~b)_i, cin);  cin(0) = 1 (the +1 of two's complement)
        a = _q(a_reg, a_lo+i)
        cout = _q(carry_anc, i+1)
        b = bbit(i)
        ops = []
        if i == 0:
            # cin = 1: majority(a, nb, 1) = a OR nb.  nb = NOT b.
            if b is None:
                # nb = 1: a OR 1 = 1
                ops.append(_g('x', cout))
            else:
                # a OR (NOT b) = NOT( (NOT a) AND b );  build via x-wrapped ccx
                ops.append(_g('x', a))
                ops.append(_g('ccx', a, b, cout))
                ops.append(_g('x', a))
                ops.append(_g('x', cout))
        else:
            cin = _q(carry_anc, i)
            if b is None:
                # nb = 1: majority(a, 1, cin) = a OR cin
                ops.append(_g('cx', a, cout))
                ops.append(_g('cx', cin, cout))
                ops.append(_g('ccx', a, cin, cout))
            else:
                # majority(a, NOT b, cin): flip b, do the standard 3-term majority, flip b back
                ops.append(_g('x', b))
                ops.append(_g('ccx', a, b, cout))
                ops.append(_g('ccx', b, cin, cout))
                ops.append(_g('ccx', a, cin, cout))
                ops.append(_g('x', b))
        return ops

    fwd = []
    for i in range(a_width):
        fwd += carry_ops(i)
    out = ''.join(fwd)
    # a >= b  <=>  carry into bit a_width is set
    out += _g('cx', _q(carry_anc, a_width), cmp_anc)   # copy predicate out
    if less_than:
        out += _g('x', cmp_anc)                         # a < b is the complement of a >= b
        out += _g('z', cmp_anc)                         # phase-flip where a < b
        out += _g('x', cmp_anc)
    else:
        out += _g('z', cmp_anc)                         # phase-flip where a >= b
    out += _g('cx', _q(carry_anc, a_width), cmp_anc)    # uncopy predicate
    out += ''.join(reversed(fwd))                       # uncompute the carry chain
    return out


def loop4(q_residue: str,
          q_residue_w: int,
          q_acc: str,
          acc_w: int,
          trunc: int,
          window4: int,
          lookup_out: str,
          cmp_out: str,
          unary_it_anc: str,
          carry_anc: str,
          cmp_anc: str) -> str:
    '''
        Adds `q_residue`'s approximate contribution into the accumulator `q_acc`
        (Gidney 2025, Appendix A.1 `loop4`): `Q_acc += Q_residue * (L//p) * pow(L//p,-1,p)`
        mod the truncated modulus. Self-contained -- there is no `unloop4`, so the inline
        phase corrections are emitted here.

            `q_residue`: residue register (input, from `loop3`), width `q_residue_w`.
            `q_acc`: accumulator register, width `acc_w` (top qubit is the wrap/sign bit).
            `trunc`: the truncated modulus (classical).
            `window4`: address window size (w4).
            `lookup_out`, `cmp_out`: temp registers for the `table` and `table2` lookups,
                width `trunc.bit_length()`.
            `unary_it_anc`: unary-iteration ancilla. 
            `carry_anc`: ripple-adder ancilla
                (width `acc_w+1`).  `cmp_anc`: single qubit for the comparison predicate.

        Per window: a QROM lookup + a modular subtraction (q-q subtract + `ghz_lookup(trunc)`
        add-back on the wrap bit), then a phase-flip comparison and a phaseup that resolves the
        lookup's vent. Emission is in-place; the accumulated result is left in `q_acc`.
    '''
    out = ''
    mod_w = trunc.bit_length()
    num_windows4 = int(math.ceil(q_residue_w / window4))
    for j in range(num_windows4):
        lb = j*window4
        addr = [_q(q_residue, lb+b) for b in range(window4) if lb+b < q_residue_w]

        # Q_acc -= table[Q_k]  (lookup + q-q subtraction; borrow lands in the wrap bit)
        out += _loop3_lookup(addr, lookup_out, mod_w, unary_it_anc)
        out += _ripple_carry_qq_adder(q_acc, 0, acc_w, lookup_out, 0, mod_w, carry_anc, sub=True)
        # measurement-uncompute the main lookup output (its vent is resolved by the phaseup below)
        for b in range(mod_w):
            out += _g('mx', _q(lookup_out, b))

        # mod-reduction add-back: Q_acc[:-1] += trunc, controlled on the wrap bit
        out += _ripple_carry_const_adder(q_acc, 0, acc_w-1, trunc, carry_anc,
                                         ctrl=_q(q_acc, acc_w-1))
        # measure-clear the wrap bit
        out += _g('mx', _q(q_acc, acc_w-1))

        # phase-flip comparison: qpu.z(Q_acc[:-1] >= table2[Q_k])   (table2 = trunc - table)
        out += _loop3_lookup(addr, cmp_out, mod_w, unary_it_anc)
        out += _ripple_carry_compare(q_acc, 0, acc_w-1, cmp_out, 0, mod_w, carry_anc, cmp_anc)
        for b in range(mod_w):
            out += _g('mx', _q(cmp_out, b))

        # phaseup resolving the main lookup's vent: qpu.z(table.vent[Q_k])
        out += _phaseup(addr, unary_it_anc)
    return out

#################################################################
#################################################################

def unloop2(q_target: str,
            q_target_w: int,
            modulus: int,
            compressed_len: int,
            carry_anc: str) -> str:
    '''
        Uncompresses `q_target % modulus` back out of the low `compressed_len` qubits -- the exact
        inverse of `loop2` (Gidney 2025, Appendix A.1 `unloop2`). `n` runs from `compressed_len`
        up to `q_target_w-1`; each step reverses one loop2 division step in reverse order: first
        the controlled subtraction (inverse of loop2's ghz add-back), then the constant addition
        (inverse of loop2's subtraction).
    '''
    out = ''
    n = compressed_len
    while n < q_target_w:
        threshold = modulus << (n - compressed_len)
        # inverse of step 2: Q_target[:n] -= threshold, controlled on the sign bit q_target[n]
        out += _ripple_carry_const_adder(q_target, 0, n, threshold, carry_anc,
                                         ctrl=_q(q_target, n), sub=True)
        # inverse of step 1: Q_target[:n+1] += threshold
        out += _ripple_carry_const_adder(q_target, 0, n+1, threshold, carry_anc)
        n += 1
    return out


def unloop3(q_unresult: str,
            q_unresult_w: int,
            q_helper: str,
            q_dlog: str,
            q_dlog_w: int,
            modulus: int,
            window3a: int,
            window3b: int,
            lookup_out: str,
            cmp_out: str,
            unary_it_anc: str,
            carry_anc: str,
            cmp_anc: str) -> str:
    '''
        Uncomputes `pow(generator, q_dlog, modulus)` held in `q_unresult` (Gidney 2025, Appendix
        A.1 `unloop3`) -- the reverse of `loop3`. For each exponent window `j` (descending), two
        residue-window inner loops (multiply by X^-1, swap, un-multiply by X) each do a QROM
        lookup + modular subtraction, a `<` phase-flip comparison, and a phaseup. The
        classically-controlled Pauli corrections (`qpu.z(not_phase_wrap)`, `qpu.cz(Q_helper,
        phase_mask)`) are omitted as free frame updates. The `q_unresult`/`q_helper` swap is
        emitted as a register-level `swap` (a free relabel on FT hardware). Ends by
        measurement-uncomputing both registers and resolving the init lookup's vent with a phaseup.

            `q_unresult`, `q_helper`: the two working registers, each width `modulus.bit_length()+1`.
            `q_dlog`: the (compressed) exponent, width `q_dlog_w`.
            other args: as in `loop3`/`loop4`.
    '''
    out = ''
    mod_w = modulus.bit_length()
    reg_w = mod_w + 1
    num_windows3a = int(math.ceil(q_dlog_w / window3a))
    num_windows3b = int(math.ceil(mod_w / window3b))

    def inner_block(helper: str, addr: list) -> str:
        # one k-iteration, shared by both inner loops (Q_helper -= table2[Q_l] + phase fixups)
        s = ''
        # lookup table2[Q_l] and subtract it (borrow lands in the wrap bit)
        s += _loop3_lookup(addr, lookup_out, mod_w, unary_it_anc)
        s += _ripple_carry_qq_adder(helper, 0, reg_w, lookup_out, 0, mod_w, carry_anc, sub=True)
        for b in range(mod_w):
            s += _g('mx', _q(lookup_out, b))            # uncompute lookup (vent resolved below)
        # mod-fix add-back, controlled on the wrap bit; then measure the wrap bit
        s += _ripple_carry_const_adder(helper, 0, mod_w, modulus, carry_anc, ctrl=_q(helper, mod_w))
        s += _g('mx', _q(helper, mod_w))
        # phase-flip comparison: qpu.z(Q_helper[:-1] < table1[Q_l])
        s += _loop3_lookup(addr, cmp_out, mod_w, unary_it_anc)
        s += _ripple_carry_compare(helper, 0, mod_w, cmp_out, 0, mod_w, carry_anc, cmp_anc,
                                   less_than=True)
        for b in range(mod_w):
            s += _g('mx', _q(cmp_out, b))
        # phaseup resolving the lookup's vent
        s += _phaseup(addr, unary_it_anc)
        return s

    for j in reversed(range(2, num_windows3a)):
        expo_lb = j*window3a
        expo_addr = [_q(q_dlog, expo_lb+b) for b in range(window3a) if expo_lb+b < q_dlog_w]

        # inner loop 1: Q_helper := Q_unresult * X^-1 % N
        for k in reversed(range(num_windows3b)):
            res_lb = k*window3b
            res_addr = [_q(q_unresult, res_lb+b) for b in range(window3b) if res_lb+b < mod_w]
            out += inner_block(q_helper, res_addr + expo_addr)   # Q_l = (Q_l0<<w3b)|Q_l1

        # (omit qpu.cz(Q_helper, phase_mask) -- classically-controlled Paulis, free frame update)
        # swap roles (register-level SWAP; a free relabel on FT hardware)
        out += _g('swap', q_unresult, q_helper)

        # inner loop 2: del Q_helper := Q_unresult * X % N
        for k in reversed(range(num_windows3b)):
            res_lb = k*window3b
            res_addr = [_q(q_unresult, res_lb+b) for b in range(window3b) if res_lb+b < mod_w]
            out += inner_block(q_helper, res_addr + expo_addr)

    # init cleanup: measurement-uncompute both registers, then resolve the init lookup's vent
    for b in range(reg_w):
        out += _g('mx', _q(q_helper, b))
    for b in range(reg_w):
        out += _g('mx', _q(q_unresult, b))
    init_bits = min(2*window3a, q_dlog_w)
    init_addr = [_q(q_dlog, b) for b in range(init_bits)]
    out += _phaseup(init_addr, unary_it_anc)
    return out

#################################################################
#################################################################

if __name__ == '__main__':
    # Parameters for RSA-2048 ----
    n = 2048
    s = 8,
    ell = 21
    w1 = 6
    w3 = 3
    w4 = 5
    f = 33
    m = 1280

    # read text file containing residue primes:
    MAX_PRIMES = 256
    with open('bisquit/residue_primes_21.txt', 'r') as rd:
        residue_primes = [ int(ln) for ln in rd.readlines() ]
        residue_primes = residue_primes[:MAX_PRIMES]
    print(f'Found {len(residue_primes)} residue primes')
    total_primes_required = int( (n*m) / (ell*w1) )

    output_file = f'bisquit/qasm/gidney25_rsa{n}_{len(residue_primes)}_of_{total_primes_required}.qasm'

    Q_EXPONENT = 'expo'
    Q_DLOG = 'dlog'
    Q_DLOG_PARTIAL = 'dlog_ps'
    Q_DLOG_DELTA = 'dlog_del'
    Q_RESULT = 'result'
    Q_RESIDUE = 'residue'

    UIT_ANC = 'unary_it_anc'
    CARRY_ANC = 'carry_anc'
    LOOKUP_ANC = 'lookup_anc'
    LOOKUP2_ANC = 'lookup2_anc'

    LOOP3_HELPER = 'lp3_helper'
    LOOP4_CMP_ANC = 'lp4_cmp_anc'

    with open(output_file, 'w') as wr:
        # preamble:
        wr.write(f'''OPENQASM 2.0;
`include "qelib1.inc";

qreg {Q_EXPONENT}[{m}];

qreg {Q_DLOG}[{f}];
qreg {Q_DLOG_PARTIAL}[{f}];
qreg {Q_DLOG_DELTA}[{f}];
qreg {Q_RESULT}[{f+1}];
qreg {Q_RESIDUE}[{ell+1}];

qreg {UIT_ANC}[{w1}];
qreg {CARRY_ANC}[{f+1}];
qreg {LOOKUP_ANC}[{ell+1}];
qreg {LOOKUP2_ANC}[{ell+1}];
qreg {LOOP3_HELPER}[{ell+1}];
qreg {LOOP4_CMP_ANC}[{f+1}];

''')
        for p in residue_primes:
            wr.write(loop1(Q_DLOG, Q_DLOG_PARTIAL, Q_DLOG_DELTA, f, Q_EXPONENT, UIT_ANC, w1, m))
            wr.write(loop2(Q_DLOG, f, p-1, ell, CARRY_ANC))

            wr.write(loop3(Q_DLOG,
                           ell, 
                           Q_RESIDUE,
                           LOOP3_HELPER,
                           p-1,
                           w3,
                           w3,
                           LOOKUP_ANC,
                           UIT_ANC,
                           CARRY_ANC))
            wr.write(loop4(Q_RESIDUE,
                           ell+1,
                           Q_RESULT,
                           f+1,
                           p-1,
                           w4,
                           LOOKUP_ANC,
                           LOOKUP2_ANC,
                           UIT_ANC,
                           CARRY_ANC,
                           LOOP4_CMP_ANC))
            wr.write(unloop3(Q_RESIDUE,
                             ell+1,
                             LOOP3_HELPER,
                             Q_DLOG,
                             ell,
                             p-1,
                             w3,
                             w3,
                             LOOKUP_ANC,
                             LOOKUP2_ANC,
                             UIT_ANC,
                             CARRY_ANC,
                             LOOP4_CMP_ANC
                            ))
            wr.write(unloop2(Q_DLOG, f, p-1, ell, CARRY_ANC))
    os.system(f'xz -z -T 16 {output_file}')



#################################################################
#################################################################
