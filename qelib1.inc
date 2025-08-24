OPENQASM 2.0;

//  author: Suhas Vittal
// 
//  This is Quicksilver's special `qelib1.inc` for its basis gate set:
//      h, x, y, z, s, sdg, t, tdg, rx, ry, rz, cx, cz, ccx, ccz, measure
//  Note that this isn't the standard gate set assumed for OpenQASM 2.0.
// 

gate id a
{
    // NOP
}

gate reset a
{  
    // this is a nop for simulation purposes (X-based reset can occur in software)
}

gate cswap a, b, c
{
    cx c, b;
    ccx a, b, c;
    cx c, b;
}

gate u(X,Y,Z) a
{
    rz(Z) a;
    sx a;
    z a;
    rz(X) a;
    sx a;
    z a;
    rz(Y) a;
}

gate p(X) a
{
    rz(X) a; 
}

gate cp(X) a, b
{
    rz(X/2) a;
    cx a, b;
    rz(-X/2) b;
    cx a, b;
    rz(X/2) b;
}

//////////////////////////////////////
//  CONTROLLED UNITARY GATES        //
//////////////////////////////////////

gate cu(X,Y,Z,W) a, b
{
    rz(W + (Y+Z)/2) a;
    rz((Z-Y)/2)     b;

    cx a, b;

    rz((-Y-Z)/2) b;
    sx           b;
    z            b;
    rz(-X/2)     b;
    sx           b;
    z            b;
    
    cx a, b;

    sx           b;
    z            b;
    rz(X/2)      b;
    sx           b;
    z            b;
    rz(Y)        b;
}

gate cu1(X) a, b
{
    rz(X/2) a;
    cx a, b;
    rz(-X/2) b;
    cx a, b;
    rz (X/2) b;
}

gate cu3(X,Y,Z) a, b
{
    cu(X,Y,Z,0) a, b;
}

//////////////////////////////////////
//  RY AND CTRL'd ROTATION GATES    //
//////////////////////////////////////

gate ry(X) a
{
    sdg a;
    rx(X) a;
    s a;
}

gate crx(X) a, b
{
    h b;
    rx(X/2) b;
    cx a, b;
    rx(-X/2) b;
    cx a, b;
    h b;
}

gate cry(X) a, b
{
    sx b;
    rz(X/2) b;
    cx a, b;
    rz(-X/2) b;
    cx a, b;
    sxdg b;
}

gate crz(X) a, b
{
    rz(X/2) b;
    cx a, b;
    rz(-X/2) b;
    cx a, b;
}

//////////////////////////////////////
//  Ux GATES (for compatibility)    //
//////////////////////////////////////

gate u1(X) a
{
    rz(X) a;
}

gate u2(X,Y) a
{
    sdg a;
    rz(Y) a;
    sx a;
    rz(X) a;
    s a;
}

gate u3(X,Y,Z) a
{
    u(X,Y,Z) a;
}

//////////////////////////////////////
// RZZ, RZX, etc.                   //
//////////////////////////////////////

gate rzz(X) a, b
{
    cx a, b;
    rz(X) b;
    cx a, b;
}

gate rzx(X) a, b
{
    h b;
    rzz(X) a, b;
    h b;
}

gate ryy(X) a, b
{
    sx a;
    sx b;
    rzz(X) a, b;
    sxdg a;
    sxdg b;
}

gate rxx(X) a, b
{
    h a;
    h b;
    rzz(X) a, b;
    h a;
    h b;
}