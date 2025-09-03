#!/usr/bin/env python3
from z3 import Extract, Concat, Not, ZeroExt, sat, Or, BitVec, Solver
import random
from enum import IntEnum, auto
import sys


class UnOp(IntEnum):
    NONE = 0
    REV = auto()
    SWAP = auto()
    NOT = auto()


class BinOp(IntEnum):
    ADD = 0
    SUB = auto()
    MUL = auto()
    AND = auto()
    OR = auto()
    XOR = auto()
    PACKHI = auto()
    PACKLO = auto()


class Operand:
    t = None
    p = None
    v = None
    m = None
    b = None
    def __init__(self, t, p, v, m, b):
        self.t = t
        self.p = p
        self.v = v
        self.m = m
        self.b = b


"""
Reverse the bits in a number
"""
def reverse_bits(x, w):
    y = 0
    for _ in range(w):
        y = (y << 1) | (x % 2 == 1)
        x = x >> 1
    return y


"""
Swap halves of a number
"""
def swap_halves(x, w):
    y = 0
    if w == 32:
        y = ((x & 0xFFFF0000) >> 16) | ((x & 0x0000FFFF) << 16)
    elif w == 16:
        y = ((x & 0xFF00) >> 8) | ((x & 0x00FF) << 8)
    elif w == 8:
        y = ((x & 0xF0) >> 4) | ((x & 0x0F) << 4)
    return y


"""
Pack high halves into a number
"""
def packhi(x, y, w):
    z = 0
    if w == 32:
        z = (x & 0xFFFF0000) | ((y & 0xFFFF0000) >> 16)
    elif w == 16:
        z = (x & 0xFF00) | ((y & 0xFF00) >> 8)
    elif w == 8:
        z = (x & 0xF0) | ((y & 0xF0) >> 4)
    return z


"""
Pack low halves into a number
"""
def packlo(x, y, w):
    z = 0
    if w == 32:
        z = (x & 0x0000FFFF) | ((y & 0x0000FFFF) << 16)
    elif w == 16:
        z = (x & 0x00FF) | ((y & 0x00FF) << 8)
    elif w == 8:
        z = (x & 0x0F) | ((y & 0x0F) << 4)
    return z


"""
Select a subset of bytes from an integer
Return the result as an Operand class object
"""
def select_operand(x):
    # Choose an operand type (byte, short, or int)
    t = random.randrange(3)

    # Choose the position of the operand (b0, b1, b2, b3, s0, s1, s2, i0)
    w = 0
    if t == 0:
        w = 8
        p = random.randrange(4)
        if   p == 0: b = (x & 0x000000FF)
        elif p == 1: b = (x & 0x0000FF00) >> 8
        elif p == 2: b = (x & 0x00FF0000) >> 16
        else:        b = (x & 0xFF000000) >> 24
    elif t == 1:
        w = 16
        p = random.randrange(3)
        if   p == 0: b = (x & 0x0000FFFF)
        elif p == 1: b = (x & 0x00FFFF00) >> 8
        else:        b = (x & 0xFFFF0000) >> 16
    else:
        w = 32
        p = 0
        b = x

    # Mutate the value with a unary operation
    m = random.choice(list(UnOp))
    m = UnOp.NONE
    if m == UnOp.REV:
        v = reverse_bits(b, w)

    elif m == UnOp.SWAP:
        v = swap_halves(b, w)

    elif m == UnOp.NOT:
        v = ~b

    else:
        v = b

    # Apply a mask just in case
    if   w == 8:  v = v & 0xFF
    elif w == 16: v = v & 0xFFFF
    else:         v = v & 0xFFFFFFFF

    # Return a new operand with that information
    return Operand(t, p, v, m, b)


"""
Calculate the result of two operands being operated on
"""
def calculate(x, y, o):
    res = 0
    if o == BinOp.ADD:
        res = x + y
    elif o == BinOp.SUB:
        res = x - y
    elif o == BinOp.MUL:
        res = x * y
    elif o == BinOp.AND:
        res = x & y
    elif o == BinOp.OR:
        res = x | y
    elif o == BinOp.XOR:
        res = x ^ y
    elif o == BinOp.PACKHI:
        res = packhi(x, y, 32)
    elif o == BinOp.PACKLO:
        res = packlo(x, y, 32)

    # Mask the result to fit in 32 bits
    res &= 0xFFFFFFFF
    return res


"""
Apply a binary operation in Z3
"""
def binop_z3(a, b, op):
    if op == BinOp.ADD:
        r = a + b
    elif op == BinOp.SUB:
        r = a - b
    elif op == BinOp.MUL:
        r = a * b
    elif op == BinOp.AND:
        r = a & b
    elif op == BinOp.OR:
        r = a | b
    elif op == BinOp.XOR:
        r = a ^ b
    elif op == BinOp.PACKHI:
        r = (a & 0xFFFF0000) | ((b & 0xFFFF0000) >> 16)
    elif op == BinOp.PACKLO:
        r = (a & 0x0000FFFF) | ((b & 0x0000FFFF) << 16)
    return r


"""
Apply a unary operation in Z3
x - Z3 variable to mutate
w - Width of the Z3 variable
op - Operation to perform
"""
def unop_z3(x, op):
    y = None

    if op == UnOp.REV:
        bits = [Extract(i, i, x) for i in range(x.size())]
        y = Concat(*(bits))

    elif op == UnOp.SWAP:
        if x.size() == 32:
            hi = Extract(31, 16, x)
            lo = Extract(15, 0, x)
            y = Concat(lo, hi)
        elif x.size() == 16:
            hi = Extract(15, 8, x)
            lo = Extract(7, 0, x)
            y = Concat(lo, hi)
        elif x.size() == 8:
            hi = Extract(7, 4, x)
            lo = Extract(3, 0, x)
            y = Concat(lo, hi)

    elif op == UnOp.NOT:
        if x.size() == 32:
            y = (~x) & 0xffffffff
        elif x.size() == 16:
            y = (~x) & 0xffff
        else:
            y = (~x) & 0xff

    else:
        y = x

    if x.size() != 32:
        return ZeroExt(32 - x.size(), y)
    return y


"""
Slice some x
"""
def create_operand_sym_from_x(name, op, x):
    # byte
    if op.t == 0:
        shift = op.p * 8
        return Extract(shift + 7, shift, x)

    # short
    elif op.t == 1:
        if op.p == 0:
            return Extract(15, 0, x)
        elif op.p == 1:
            return Extract(23, 8, x)
        else:
            return Extract(31, 16, x)

    # int
    else:
        return x


"""
Process inputs to find unique arithmetic
"""
def process_input(i, x):
    x_sym = BitVec('x', 32)
    tries = 1
    unique = False

    while True:
        operands = [select_operand(x) for _ in range(3)]
        operations = [random.choice(list(BinOp)) for _ in range(2)]

        while len(operands) < 11:
            result = operands[0].v
            for j in range(len(operations)):
                result = calculate(result, operands[j+1].v, operations[j])
            result &= 0xFFFFFFFF

            sym_operands = [create_operand_sym_from_x(f'op{k}', op, x_sym) for k, op in enumerate(operands)]
            sym_mutated = [unop_z3(sym_operands[k], operands[k].m) for k in range(len(operands))]

            r_sym = sym_mutated[0]
            for j in range(len(operations)):
                r_sym = binop_z3(r_sym, sym_mutated[j+1], operations[j])
            r_sym = r_sym & 0xFFFFFFFF

            solver = Solver()
            solver.set("timeout", 10000)
            solver.add(r_sym == result)
            solver.add(x_sym != x)

            has_alternate = (solver.check() == sat)

            if has_alternate:
                operands.append(select_operand(x))
                operations.append(random.choice(list(BinOp)))
            else:
                return operands, operations, result
        tries += 1

    return None,None,None


"""
Lift an operand to WeeperVM ASM
"""
def asm_load(op, src='R0', dst='R1'):
    code = []
    if op.t == 0:
        if op.p == 0:
            code.append(f'MOV {dst} {src}.b')
        elif op.p == 1:
            code.append(f'MOV {dst} {src}.s')
            code.append(f'SHR {dst} 8')
        elif op.p == 2:
            code.append(f'SWAP {dst} {src}')
            code.append(f'MOV {dst} {dst}.b')
        else:
            code.append(f'REV {dst} {src}')
            code.append(f'MOV {dst} {dst}.b')
            code.append(f'REV {dst}.b {dst}.b')

        if op.m == UnOp.REV:
            code.append(f'REV {dst} {dst}.b')
        elif op.m == UnOp.SWAP:
            code.append(f'SWAP {dst} {dst}.b')
        elif op.m == UnOp.NOT:
            code.append(f'NOT {dst} {dst}.b')

    elif op.t == 1:
        if op.p == 0:
            code.append(f'MOV {dst} {src}.s')
        elif op.p == 1:
            code.append(f'MOV {dst} {src}')
            code.append(f'SHL {dst} 8')
            code.append(f'SHR {dst} 16')
        else:
            code.append(f'REV {dst} {src}')
            code.append(f'MOV {dst} {dst}.s')
            code.append(f'REV {dst}.s {dst}.s')

        if op.m == UnOp.REV:
            code.append(f'REV {dst} {dst}.s')
        elif op.m == UnOp.SWAP:
            code.append(f'SWAP {dst} {dst}.s')
        elif op.m == UnOp.NOT:
            code.append(f'NOT {dst} {dst}.s')

    else:
        code.append(f'MOV {dst} {src}.i')

        if op.m == UnOp.REV:
            code.append(f'REV {dst} {dst}.i')
        elif op.m == UnOp.SWAP:
            code.append(f'SWAP {dst} {dst}.i')
        elif op.m == UnOp.NOT:
            code.append(f'NOT {dst} {dst}.i')

    return code


"""
Lift an operation to WeeperVM ASM
"""
def asm_binary(op, left='R1', right='R2'):
    return [f'{op.name} {left} {right}']


"""
Lift a compare to WeeperVM ASM
"""
def asm_compare(left, right, dst):
    return [f'CMP {left} {right}', f'RET NEQ']


"""
Driver function to generate unique constraint checking for a list of numbers
Output format is WeeperVM assembly
"""
def main(startline, endline):
    # Read the valid numbers into a list
    if startline is not None and endline is not None:
        with open('numbers.txt', 'r') as f:
            nums = [int(x, 16) for x in f.read().split('\n')[startline:endline] if x.strip()]
    else:
        with open('numbers.txt', 'r') as f:
            nums = [int(x, 16) for x in f.read().split('\n') if x.strip()]
    if endline is None:
        endline = len(nums) - 1

    # Do NOT use multiprocessing because Z3 is unpredictable >:(
    results = []
    for i, x in enumerate(nums):
        print(f'Working on line[{i+startline}] = 0x{x:08x}')
        result = process_input(i, x)
        results.append(result)

    # Convert the operand and operations into WeeperVM assembly
    code = []
    for i, result in enumerate(results):
        operands = result[0]
        operations = result[1]
        ans = result[2]
        code += [f'<LAB_ValidateNext_{i+startline}>']

        # Get WeeperVM ASM code for the first operand
        code += asm_load(operands[0], src='F0', dst='R1')

        # Contiually get WeeperVM ASM for remaining operands and the binary operations
        for j in range(1, len(operands)):
            code += asm_load(operands[j], src='F0', dst='R2')
            code += asm_binary(operations[j-1], 'R1', 'R2')

        # Get WeeperVM ASM for comparison and conditional jump
        code += asm_compare('R1', hex(ans), '<LAB_ValidateNext_Incorrect>')
        code.append('JMP <LAB_ValidateNext_Correct>')
    code.pop()
    code.append('<LAB_ValidateNext_Correct>')
    code.append('RET EQ')

    # After all processing, write to file and print to stdout
    output_path = f'unique_cnstrs({startline}-{endline}).txt'
    with open(output_path, 'w') as fout:
        fout.write('\n'.join(code) + '\n')



"""
Entry point
"""
if __name__ == '__main__':
    start = 0
    end = None
    try:
        if len(sys.argv) > 1: start = int(sys.argv[1])
        if len(sys.argv) > 2: end = int(sys.argv[2])
    except:
        print('Failed to parse arguments to integers')
    else:
        main(start, end)
