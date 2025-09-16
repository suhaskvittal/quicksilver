'''
    author: Suhas Vittal
    date:   20 September 2025

    Creates a qasm file for BISQUIT's shor benchmark.
'''

import random
import multiprocessing as mp

#################################################################
#################################################################

RSA_MODE = 64

# 128-bit prime numbers used to generate the RSA public key
#p = 249338061461969271388931484822172864483
#q = 318086665464420588304853690890664293979

# 256-bit RSA public key
if RSA_MODE == 256:
    N = 79311112543800559059544670893166856913365574321673273203857240979443239847857
    A = 329916967358087561489242136217384669929
    A_INV = 40081242936043274348142380556789957492624533695234575448927119922306278098771


# 128-bit RSA public key
if RSA_MODE == 128:
    N = 168425339336371607834480189065517156539
    A = 15286634156585511877
    A_INV = 156007382077471147457263837412622610738 

# 64 bit RSA public key
if RSA_MODE == 64:
    N = 13410852825427549511
    A = 3445096335
    A_INV = pow(A, -1, N)

# 16-bit RSA public key
if RSA_MODE == 16:
    N = 22499
    A = 230
    A_INV = 9880

NUM_BITS = RSA_MODE

#################################################################
#################################################################

# note that fixed point angles here are stored such that the LSB corresponds to pi/2
# this is the opposite of the convention used in C++
# we do this because it makes it easier to create the string representation of the angle 
# and read it from the file (see `expression.cpp`)
def create_fpa_string(x: int) -> str:
    return hex(reverse_bits(x))

#################################################################
#################################################################

def _qft_impl(qr: str, inv: bool) -> str:
    steps = []
    # initialize `rot` to pi/2 + pi/4 + ... etc.
    rot = ((1<<NUM_BITS)-1) >> 1
    for i in range(NUM_BITS):
        angle_string = create_fpa_string(rot)
        angle_string = f'fpa{2*NUM_BITS}{angle_string}' 
        steps.append(f'h {qr}[{i}];\n')
        if rot > 0:
            steps.append(f'cp({angle_string}) {qr}[{i+1}], {qr}[{i}];\n')
        # drop msb from `rot` -- this removes `pi/2**(NUM_BITS-i-1)` from `rot`
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

MAX_ITERATION = 2*NUM_BITS

if __name__ == '__main__':
    output_file = f'bisquit/qasm/shor_rsa{NUM_BITS}.qasm'

    # only do some iterations so the file isn't too large -- eventually loops will be added, but until then...
    iter_inc_freq = 8
    # iid selection:
    iter_list = []
    for i in range(0, MAX_ITERATION, iter_inc_freq):
        idx = random.randint(0, iter_inc_freq-1)
        iter_list.append(i+idx)

    a, a_inv = A, A_INV
    rot = random.randint(0, 2**(2*NUM_BITS-1)-1)

    with open(output_file, 'w') as ostrm:
        ostrm.write(f'OPENQASM 2.0;\n')
        ostrm.write(f'include "qelib1.inc";\n\n')
        ostrm.write(f'qreg c;\n')
        ostrm.write(f'qreg anc_blk[{NUM_BITS}];\n')
        ostrm.write(f'qreg anc_mod_adder;\n')
        ostrm.write(f'qreg q[{NUM_BITS}];\n\n')

        # initialization:
        ostrm.write(f'x q;\n') # vector op

        for i in range(0, MAX_ITERATION):
            a = (a*a) % N
            a_inv = (a_inv*a_inv) % N
            # remove top `2*NUM_BITS-ii` bits of rot
            mask = ((1<<(2*NUM_BITS-i))-1) << i
            _rot = rot & ~mask

            if i in iter_list:
                ostrm.write(f'// iteration {i}\n\n')
                iter_text = cua('c', 'q', 'anc_blk', 'anc_mod_adder', a, a_inv)
                ostrm.write(iter_text)
                if _rot != 0:
                    ostrm.write(f'rz(fpa{2*NUM_BITS}{hex(reverse_bits(_rot))}) c;\n')
                

#################################################################
#################################################################