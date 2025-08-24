OPENQASM 2.0;

include "qelib1.inc";

qreg ctrl;
qreg qA[3];

rz(3*pi/4) ctrl;
rz(3*pi) ctrl;
cswap qA[0], qA[1], qA[2];
u1(7*pi/8) ctrl;