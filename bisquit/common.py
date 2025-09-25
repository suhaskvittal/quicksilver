import math
from math import log2, pi

#################################################################
#################################################################

def reverse_bits(x: int, num_bits: int) -> int:
    y = 0
    for i in range(num_bits):
        b = (x>>i) & 1
        y |= (b << (num_bits-i-1))
    return y

# note that fixed point angles here are stored such that the LSB corresponds to pi/2
# this is the opposite of the convention used in C++
# we do this because it makes it easier to create the string representation of the angle 
# and read it from the file (see `expression.cpp`)
def create_fpa_string(x: int, num_bits: int) -> str:
    return hex(reverse_bits(x, num_bits))

#################################################################
#################################################################

def _qft_impl(qr: str, inv: bool, num_bits: int, max_denom: int) -> str:
    steps = []
    # initialize `rot` to pi/2 + pi/4 + ... etc.
    for i in range(num_bits):
        steps.append(f'h {qr}[{i}];\n')
        denom = 1
        for j in range(i+1, num_bits):
            if denom > max_denom:
                break
            rot = 1 << (num_bits-denom-1)
            angle_string = create_fpa_string(rot, num_bits)
            angle_string = f'fpa{2*num_bits}{angle_string}' 
            steps.append(f'cp({angle_string}) {qr}[{j}], {qr}[{i}];\n')
            denom += 1

    if inv:
        steps.reverse()
    final_string = ''.join(steps)
    return final_string

def qft(qr: str, num_bits: int, max_denom: int) -> str:
    return _qft_impl(qr, False, num_bits, max_denom)

def iqft(qr: str, num_bits: int, max_denom: int) -> str:
    return _qft_impl(qr, True, num_bits, max_denom)

def calculate_aqft_max_denom(num_bits: int, e=1e-6) -> int:
    return int(log2(2*pi*num_bits) - log2(e))

#################################################################
#################################################################

def bit_is_set(x: int, where: int):
    return x & (1<<where)

#################################################################
#################################################################