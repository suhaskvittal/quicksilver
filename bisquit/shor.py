'''
    author: Suhas Vittal
    date:   20 September 2025

    Creates a qasm file for BISQUIT's shor benchmark.
'''

import random
import multiprocessing as mp

from common import *

#################################################################
#################################################################

RSA_MODE = 256

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
QFT_DENOM = calculate_aqft_max_denom(NUM_BITS)

#################################################################
#################################################################

QFT_CACHE = {} # maps qft for given register to qasm string
IQFT_CACHE = {}

def fourier_adder(ctrl: list[str], qr: str, a: int, inv=False) -> str:
    '''
        Performs |qr> --> |a + qr>
    '''
    out_array = []
    for i in range(NUM_BITS):
#        rot_angle = [ f'pi/{j}' for j in range(NUM_BITS-1) if bit_is_set(a,j) ]
#        angle_string = ' + '.join(rot_angle)

        # need to mask top `i` bits of `a`
        rot_mask = ((1<<i)-1) << (NUM_BITS-i)
        rot = a & ~rot_mask
        if rot == 0:
            continue
        angle_string = create_fpa_string(rot, NUM_BITS)
        angle_string = f'fpa{2*NUM_BITS}{angle_string}'   # note that we need to reverse the string because the LSB of `a` corresponds to `pi` (we want MSB to correspond to `pi`)

        if len(ctrl) == 0:
            out_array.append(f'p({angle_string}) {qr}[{i}];')
        elif len(ctrl) == 1:
            out_array.append(f'cp({angle_string}) {ctrl[0]}, {qr}[{i}];')
        else:
            out_array.append(f'ccp({angle_string}) {ctrl[0]}, {ctrl[1]}, {qr}[{i}];')
    if inv:
        out_array.reverse()
    out = '\n'.join(out_array)
    return out

def mod_adder(c1: str, c2: str, qr: str, anc: str, a: int) -> str:
    '''
        Performs |qr> --> |(a+qr) mod N>
    '''
    out = ''

    ccfadd_a = fourier_adder([c1, c2], qr, a)
    ccfadd_a_inv = fourier_adder([c1, c2], qr, a, inv=True)

    if qr not in QFT_CACHE:
        QFT_CACHE[qr] = qft(qr, NUM_BITS, QFT_DENOM)
    if qr not in IQFT_CACHE:
        IQFT_CACHE[qr] = iqft(qr, NUM_BITS, QFT_DENOM)

    iqft_qr = IQFT_CACHE[qr]
    qft_qr = QFT_CACHE[qr]

    out += ccfadd_a
    out += fourier_adder([], qr, N, inv=True)
    out += iqft_qr
    out += f'cx {qr}[{NUM_BITS-1}], {anc};\n'
    out += qft_qr
    out += fourier_adder([anc], qr, N)
    out += ccfadd_a_inv
    out += iqft_qr
    out += f'x {qr}[{NUM_BITS-1}];\ncx {qr}[{NUM_BITS-1}], {anc};\nx {qr}[{NUM_BITS-1}];\n'
    out += qft_qr
    out += ccfadd_a

    return out

def cmul(c: str, qx: str, qr: str, anc: str, a: int) -> str:
    print(f'\t\t\tcmul {c}, {qx}, {qr}, {anc}, {a}')

    out = ''
    out += qft(qr, NUM_BITS, QFT_DENOM)
    for i in range(NUM_BITS):
        a <<= 1
        out += mod_adder(c, f'{qx}[{i}]', qr, anc, a)
    out += iqft(qr, NUM_BITS, QFT_DENOM)
    return out

def cua(c: str, qx: str, qr: str, anc: str, a: int, a_inv: int) -> str:
    print(f'\tcua {c}, {qx}, {qr}, {anc}, {a}, {a_inv}')

    out = ''
    out += cmul(c, qx, qr, anc, a)
    out += f'cswap {c}, {qx}, {qr};\n'
    out += cmul(c, qx, qr, anc, a_inv)
    return out

#################################################################
#################################################################

MAX_ITERATION = 2*NUM_BITS
ITER_COUNT = 4

if __name__ == '__main__':
    output_file = f'bisquit/qasm/shor_rsa{NUM_BITS}_iter_{ITER_COUNT}.qasm'

    # only do some iterations so the file isn't too large -- eventually loops will be added, but until then...
    iter_inc_freq = MAX_ITERATION//ITER_COUNT
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
            print(f'iteration {i}')
            a = (a*a) % N
            a_inv = (a_inv*a_inv) % N
            # remove top `2*NUM_BITS-ii` bits of rot
            mask = ((1<<(2*NUM_BITS-i))-1) << i
            _rot = rot & ~mask

            if i in iter_list:
                print('\t', 'writing iteration', i)
                ostrm.write(f'// iteration {i}\n\n')
                iter_text = cua('c', 'q', 'anc_blk', 'anc_mod_adder', a, a_inv)
                ostrm.write(iter_text)
#               if _rot != 0:
#                   ostrm.write(f'rz(fpa{2*NUM_BITS}{create_fpa_string(_rot, NUM_BITS)}) c;\n')
    print('qft denom: ', QFT_DENOM) 

#################################################################
#################################################################