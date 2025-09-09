'''
    author: Suhas Vittal
    date:   20 September 2025

    Creates a qasm file for BISQUIT's shor benchmark.
'''

import random
import multiprocessing as mp

#################################################################
#################################################################

# 128-bit prime numbers used to generate the RSA public key
#jp = 249338061461969271388931484822172864483
#q = 318086665464420588304853690890664293979

# 256-bit RSA public key
#N = 79311112543800559059544670893166856913365574321673273203857240979443239847857
#A = 329916967358087561489242136217384669929
#A_INV = 40081242936043274348142380556789957492624533695234575448927119922306278098771

#NUM_BITS = 256

# 128-bit RSA public key
N = 168425339336371607834480189065517156539
A = 15286634156585511877
A_INV = 156007382077471147457263837412622610738 
NUM_BITS = 128

# 16-bit RSA public key
#N = 22499
#A = 230
#A_INV = 9880
#NUM_BITS = 16

#################################################################
#################################################################

def _qft_impl(qr: str, inv: bool) -> str:
    steps = []
    rot = ((1<<NUM_BITS)-1) >> 1
    for i in range(NUM_BITS):
#       rot_angle = [ f'pi/{j+1}' for j in range(i+1, NUM_BITS) ]
#       angle_string = ' + '.join(rot_angle)
        angle_string = hex(reverse_bits(rot))
        angle_string = f'fpa{2*NUM_BITS}{angle_string}' 
        steps.append(f'h {qr}[{i}];\n')
        if rot > 0:
            steps.append(f'cp({angle_string}) {qr}[{i+1}], {qr}[{i}];\n')
        rot >>= 1

    if inv:
        steps.reverse()
    final_string = ''.join(steps)
    return final_string

def qft(qr: str) -> str:
    return _qft_impl(qr, False)

def iqft(qr: str) -> str:
    return _qft_impl(qr, True)

#################################################################
#################################################################

def bit_is_set(x: int, where: int):
    return x & (1<<where)

def reverse_bits(x: int) -> int:
    y = 0
    for i in range(NUM_BITS):
        b = (x>>i) & 1
        y |= (b << (NUM_BITS-i-1))
    return y

def fourier_adder(ctrl: list[str], qr: str, a: int) -> str:
    '''
        Performs |qr> --> |a + qr>
    '''
    out = ''
    for i in range(NUM_BITS):
#        rot_angle = [ f'pi/{j}' for j in range(NUM_BITS-1) if bit_is_set(a,j) ]
#        angle_string = ' + '.join(rot_angle)

        # need to mask top `i` bits of `a`
        rot_mask = ((1<<i)-1) << (NUM_BITS-i)
        rot = a & ~rot_mask
        if rot == 0:
            continue
        angle_string = hex(reverse_bits(rot))
        angle_string = f'fpa{2*NUM_BITS}{angle_string}'   # note that we need to reverse the string because the LSB of `a` corresponds to `pi` (we want MSB to correspond to `pi`)

        if len(ctrl) == 0:
            out += f'p({angle_string}) {qr}[{i}];\n'
        elif len(ctrl) == 1:
            out += f'cp({angle_string}) {ctrl[0]}, {qr}[{i}];\n'
        else:
            out += f'ccp({angle_string}) {ctrl[0]}, {ctrl[1]}, {qr}[{i}];\n'
    return out

def mod_adder(c1: str, c2: str, qr: str, anc: str, a: int) -> str:
    '''
        Performs |qr> --> |(a+qr) mod N>
    '''
    out = ''

    ccfadd_a = fourier_adder([c1, c2], qr, a)
    iqft_qr = iqft(qr)
    qft_qr = qft(qr)

    out += ccfadd_a
    out += fourier_adder([], qr, N)
    out += iqft_qr
    out += f'cx {qr}[{NUM_BITS-1}], {anc};\n'
    out += qft_qr
    out += fourier_adder([anc], qr, N)
    out += ccfadd_a
    out += iqft_qr
    out += f'x {qr}[{NUM_BITS-1}];\ncx {qr}[{NUM_BITS-1}], {anc};\nx {qr}[{NUM_BITS-1}];\n'
    out += qft_qr
    out += ccfadd_a

    return out

def cmul(c: str, qx: str, qr: str, anc: str, a: int) -> str:
    out = ''
    out += qft(qr)
    for i in range(NUM_BITS):
        a <<= 1
        out += mod_adder(c, f'{qx}[{i}]', qr, anc, a)
    out += iqft(qr)
    return out

def cua(c: str, qx: str, qr: str, anc: str, a: int, a_inv: int) -> str:
    out = ''
    out += cmul(c, qx, qr, anc, a)
    for i in range(NUM_BITS):
        out += f'cswap {c}, {qx}[{i}], {qr}[{i}];\n'
    out += cmul(c, qx, qr, anc, a_inv)
    return out

#################################################################
#################################################################

NUM_PROCESSES = 8
NUM_ITERATIONS = 2*NUM_BITS

ITERATION_TEXT = ['' for _ in range(NUM_PROCESSES)]

def _aggregate_iterations(q: mp.Queue):
    output_file = f'bisquit/qasm/shor_rsa{NUM_BITS}.qasm'
    base_iter = 0

    with open(output_file, 'w') as ostrm:
        ostrm.write(f'OPENQASM 2.0;\n')
        ostrm.write(f'include "qelib1.inc";\n\n')
        ostrm.write(f'qreg c;\n')
        ostrm.write(f'qreg anc_blk[{NUM_BITS}];\n')
        ostrm.write(f'qreg anc_mod_adder;\n')
        ostrm.write(f'qreg q[{NUM_BITS}];\n\n')

        # initialization:
        ostrm.write(f'x q;\n') # vector op

        while base_iter < NUM_ITERATIONS:
            print('building iterations ', base_iter, ' to ', base_iter+NUM_PROCESSES)
            cnt = 0
            while cnt < len(ITERATION_TEXT):
                (out, idx) = q.get()
                ITERATION_TEXT[idx-base_iter] = out
                cnt += 1

            # write iterations to file
            for j in range(NUM_PROCESSES):
                ostrm.write(f'\n\n// iteration {base_iter+j}\n\n')
                ostrm.write(ITERATION_TEXT[j])

            # update current base iter
            base_iter += NUM_PROCESSES

def _generate_loop_iter(a: int, a_inv: int, rot: int, iter_idx: int, q: mp.Queue):
    out = cua('c', 'q', 'anc_blk', 'anc_mod_adder', a, a_inv)
    out += f'rz(fpa{2*NUM_BITS}{hex(reverse_bits(rot))}) c;\n'
    q.put((out, iter_idx))

if __name__ == '__main__':
    a, a_inv = A, A_INV
    rot = random.randint(0, 2**(2*NUM_BITS-1)-1)

    shared_queue = mp.Queue()

    p_reduce = mp.Process(target=_aggregate_iterations, args=(shared_queue,))
    p_reduce.start()

    for i in range(0, NUM_ITERATIONS, NUM_PROCESSES):
        p_iter_list = []
        for j in range(NUM_PROCESSES):
            ii = i+j
            a = (a*a) % N
            a_inv = (a_inv*a_inv) % N
            # remove top `2*NUM_BITS-ii` bits of rot
            mask = ((1<<(2*NUM_BITS-ii))-1) << ii
            _rot = rot & ~mask

            p_iter = mp.Process(target=_generate_loop_iter, args=(a, a_inv, rot, ii, shared_queue))
            p_iter.start()
            p_iter_list.append(p_iter)

        for p_iter in p_iter_list:
            p_iter.join()

    p_reduce.join()

#################################################################
#################################################################