#!/usr/bin/env python3
import os
import random
import re
import sys
from enum import IntEnum, auto

def ROR32(x, r): return (((x) >> (r)) | ((x) << (32 - (r)))) & 0xFFFFFFFF
def ROL32(x, r): return (((x) << (r)) | ((x) >> (32 - (r)))) & 0xFFFFFFFF

# Opcode Enum and related dictionary
class Opcode(IntEnum):
    # Data movement
    MOV = 0
    LEA = auto()
    PUT = auto()
    GET = auto()
    # Arithmetic
    ADD = auto()
    SUB = auto()
    MUL = auto()
    DIV = auto()
    MOD = auto()
    # Control flow
    CMP = auto()
    JMP = auto()
    CALL = auto()
    RET = auto()
    EXIT = auto()
    # Bitwise
    AND = auto()
    OR = auto()
    XOR = auto()
    SHL = auto()
    SHR = auto()
    NOT = auto()
    # Special
    SYSCALL = auto()
    SWAP = auto()
    REV = auto()
    PACKHI = auto()
    PACKLO = auto()
    ROL = auto()
    ROR = auto()
    FORK = auto()
OPCODES = { op.name: op.value for op in Opcode }


# Register Dictionary
# P is persistent data
# R is scratch data
# F is function parameter
# NOTE: Return stack and vflags abstracted away from assembly, thus not in the compiling tool
REGS = {
 'P0': 0x00, 'P1': 0x01, 'P2': 0x02, 'P3': 0x03,
 'R0': 0x04, 'R1': 0x05, 'R2': 0x06, 'R3': 0x07, 'R4': 0x08, 'R5': 0x09,
 'F0': 0x0A, 'F1': 0x0B, 'F2': 0x0C, 'F3': 0x0D,
 'C0': 0x0E, 'C1': 0x0F }


# Flag masks for conditional jumps
FLAGS = { 'EQ':  0b0001, 'NEQ': 0b0110,
          'LT':  0b0010, 'LTE': 0b0011,
          'GT':  0b0100, 'GTE': 0b0101,
          'ERR': 0b1000 }


# Operand types
T_NONE = 0b00
T_REG  = 0b01
T_MEM  = 0b10
T_IMM  = 0b11


# Operand value types
V_NULL  = 0b00
V_BYTE  = 0b01
V_SHORT = 0b10
V_INT   = 0b11


#===========================================================================================================#
#===========================================================================================================#
#===========================================================================================================#


"""
Parse an operand as a register
"""
def parse_register(op):
    # Split the operand
    chunks = op.split('.')
    base = chunks[0]

    # Parse the value type
    v = V_INT
    if len(chunks) > 1:
        if chunks[1] == 'b': v = V_BYTE
        elif chunks[1] == 's': v = V_SHORT
        elif chunks[1] == 'i': v = V_INT
        else:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid v for register ({chunks[1]})')
            exit()

    # Parse the value
    if base.upper() not in REGS:
        print(f'\033[1m\033[31mERROR: \033[37mInvalid register ({base})')
        exit()
    x = REGS[base.upper()]

    # Return that information
    return T_REG, v, 0, x


"""
Parse an operand as a memory
"""
def parse_memory(op):
    # Subtraction flag
    m = 0

    # Clean and potentially split the operand
    chunks = op.split('].')
    chunks[0] = chunks[0].replace('[','').replace(']','')
    modifiers = chunks[0].split('+')
    if len(modifiers) == 1:
        modifiers = chunks[0].split('-')
        if len(modifiers) != 1: m = 1

    # Validate the number of sub-operands
    if len(modifiers) > 2:
        print(f'\033[1m\033[31mERROR: \033[37mToo many memory modifiers ({op})')
        exit()

    # Parse the value type
    v = V_INT
    if len(chunks) > 1:
        if chunks[1] == 'b': v = V_BYTE
        elif chunks[1] == 's': v = V_SHORT
        elif chunks[1] == 'i': v = V_INT
        else:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid v for register ({chunks[1]})')
            exit()

    # Process the sub-operands
    ti = 0; vi = 1; li = 2; xi = 3
    base = parse_operand(modifiers[0])
    disp = None
    if len(modifiers) == 2:
        disp = parse_operand(modifiers[1])

    # Spoof a zero displacement when there is a register base with no displacement
    if base[ti] == T_REG and disp is None:
        disp = (T_IMM, V_BYTE, 1, 0)

    # Compound memory access
    if disp is not None:
        # Validate the base is a register
        if base[ti] != T_REG:
            print(f'\033[1m\033[31mERROR: \033[37mBase must be a register ({op})')
            exit()

        # Validate the displacement
        if disp[ti] == T_MEM:
            print(f'\033[1m\033[31mERROR: \033[37mDisplacement cannot be memory ({op})')
            exit()

        # Displacement is a register
        if disp[ti] == T_REG:
            x = 0x3000 | (base[vi] << 10) | (disp[vi] << 8) | (base[xi] << 4) | disp[xi]
            x <<= 2
            return T_MEM, v, 1, x

        # Displacement is an immediate
        elif disp[ti] == T_IMM:
            if disp[xi].bit_length() > 20:
                print(f'\033[1m\033[31mERROR: \033[37mDisplacement exceeds 20 bits (0x{disp[xi]:x})')
                exit()

            if disp[xi].bit_length() + 7 // 8 == 0:
                shl = 8
            else:
                shl = ((disp[xi].bit_length() + 7) // 8) * 8

            x = ((0b10000000 | (base[vi] << 4) | base[xi]) << shl) | disp[xi]
            l = (x.bit_length() + 7) // 8 or 1
            l -= 1

            return T_MEM, v, l, x

    # Immediate memory access
    else:
        # Validate the immediate
        if base[ti] == T_REG:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid memory offset ({op})')
            exit()
        if base[xi].bit_length() > 20:
            print(f'\033[1m\033[31mERROR: \033[37mDisplacement exceeds 20 bits (0x{base[xi]:x})')
            exit()
        return T_MEM, v, base[li] + 1, base[xi]


"""
Parse an operand as an immediate
"""
def parse_immediate(op):
    # Immediate is a flag
    if op in FLAGS:
        return T_IMM, V_BYTE, 0, FLAGS[op]

    # Split the operand
    chunks = op.split('.')
    base = chunks[0]

    # Parse the value type
    v = V_INT
    if len(chunks) > 1:
        if chunks[1] == 'b': v = V_BYTE
        elif chunks[1] == 's': v = V_SHORT
        elif chunks[1] == 'i': v = V_INT
        else:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid v for immediate ({chunks[1]})')
            exit()

    # Parse the value
    x = 0
    try:
        if base.lower().startswith('0x'): x = int(base, 16)
        elif base.lower().startswith('0b'): x = int(base, 2)
        elif base.startswith('0'): x = int(base, 8)
        else: x = int(base, 10)
    except:
        print(f'\033[1m\033[31mERROR: \033[37mInvalid immediate format ({base})')
        exit()

    # Deduce the length
    l = (x.bit_length() + 7) // 8 or 1
    if l > 4:
        print(f'\033[1m\033[31mERROR: \033[37mImmediate exceeds 4 bytes ({base})')
        exit()
    l -= 1

    # Return that information
    return T_IMM, v, l, x


"""
Parse an operand without knowing its type
"""
def parse_operand(op):
    if op[0].upper() == 'P' or op[0].upper() == 'R' or op[0].upper() == 'F' or op[0].upper() == 'C': return parse_register(op)
    elif op[0] == '[': return parse_memory(op)
    else: return parse_immediate(op)


#===========================================================================================================#
#===========================================================================================================#
#===========================================================================================================#


"""
Driver function to compile WeeperVM v2 code
"""
def main():
    # Read the asssembly
    asm = []
    with open('../Compiler/code.weep', 'r') as f:
        asm = f.read()
    asm = [a for a in asm.split('\n') if a and len(a.strip()) != 0 and a.strip()[0] != ';']

    # Record global constant values
    print(f'[+] Searching for global constants')
    consts = {}
    for line in asm:
        if line[0] != '{': break
        clean = line.split(';')[0].strip()
        chunks = re.sub(r'\s+', ' ', clean[1:-1]).split(' ')
        print(f'[*] {chunks[0]} = {chunks[1]}')
        if chunks[0] in consts:
            print(f'\033[1m\033[31mERROR: \033[37mDuplicate global constant ({chunks[0]})')
            exit()
        consts[chunks[0]] = chunks[1]
    print(f'[+] Found {len(consts)} global constants\n')

    # Skip the variable lines
    asm = asm[len(consts):]

    # Track the index of instructions with a label
    labels = {}
    labelRefs = {}

    # Convert the assembly into bytes
    print(f'[+] Compiling...')
    line = 0
    compiled = []
    fallthroughs = []
    for insn in asm:
        # Reduce to single spaces and remove inline comments
        insn = re.sub(r'\s+', ' ', insn).split(';')[0].strip()

        # Print the clean instruction
        print(f'[{line:<4d}]  {insn}')

        # Check for a label
        if insn[0] == '<' and insn[-1] == '>':
            lbl = insn[1:-1]
            if lbl in labels:
                print(f'\033[1m\033[31mERROR: \033[37mDuplicate label ({lbl})')
                exit()
            labels[lbl] = len(compiled)
            continue

        # Split the instruction into chunks
        chunks = insn.split(' ')

        # Ensure a valid number of operands
        if len(chunks) > 3:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid number of operands ({len(chunks) - 1})')
            return

        # Select the opcode
        chunks[0] = chunks[0].upper()
        if chunks[0] not in OPCODES:
            print(f'\033[1m\033[31mERROR: \033[37mInvalid operation ({chunks[0]})')
            return
        opcode_l = random.getrandbits(5)
        opcode_o = random.getrandbits(2)
        if opcode_o == 0: opcode_r = (opcode_l ^ OPCODES[chunks[0]]) & 0b11111 # c = l ^ r
        elif opcode_o == 1: opcode_r = (~(opcode_l ^ OPCODES[chunks[0]])) & 0b11111 # c = l ^ ~r
        elif opcode_o == 2: opcode_r = (~opcode_l ^ OPCODES[chunks[0]]) & 0b11111 # c = ~l ^ r
        elif opcode_o == 3: opcode_r = ~(opcode_l ^ ~OPCODES[chunks[0]]) & 0b11111 # c = ~l ^ ~r

        # Parse the operands, checking for label references
        ops = []
        for i, op in enumerate(chunks[1:]):
            # EOL Comment
            if op[0] == ';': break

            # Label reference... use 3-byte placeholder
            elif op[0] == '<' and op[-1] == '>':
                lbl = op[1:-1]
                if lbl not in labelRefs:
                    labelRefs[lbl] = []
                labelRefs[lbl].append(len(compiled))
                op = '0x112233'

            # Replace global constants
            for c in consts:
                if c in op:
                    op = op.replace(c, consts[c])

            # Perform parsing
            ops.append(parse_operand(op))
        t = 0; v = 1; l = 2; x = 3

        # Pad operands up to a length of two
        if len(ops) == 0:
            ops = [ (T_NONE, V_NULL, 0, 0), (T_NONE, V_NULL, 0, 0) ]
        elif len(ops) == 1:
            ops.append((T_NONE, V_NULL, 0, 0))

        # Compile the instruction header
        header = (opcode_l << 19)
        header |= (ops[0][t] << 17) | (ops[0][v] << 15) | (ops[0][l] << 13)
        header |= (opcode_o << 11)
        header |= (ops[1][t] << 9) | (ops[1][v] << 7) | (ops[1][l] << 5)
        header |= (opcode_r << 0)

        # DEBUG printing
        # print(f'\tHEADER: {header:x} = {header:024b}')
        # operands = None
        # for op in ops:
        #     if op[t] != T_NONE:
        #         if operands is None: operands = 0
        #         operands = (operands << ((op[l] + 1) * 8)) | op[x]
        # if operands is not None:
        #     print(f'\tOPERANDS: {operands:x} = {operands:b}')

        # Combine the header and operands
        packed = header.to_bytes(3, 'big')
        for op in ops:
            if op[t] != T_NONE:
                packed += op[x].to_bytes(op[l]+1, 'big')

        # Calculate the fallthrough offset
        fallthrough = sum(len(s) for s in compiled) + len(packed) + 3
        # packed += fallthrough.to_bytes(3, 'little')
        packed += b'\xff\xff\xff'

        # Update the compiled instruction list
        fallthroughs.append(fallthrough)
        compiled.append(packed)
        line += 1

    print()

    # Shuffle the instruction order, excluding the first instruction
    print(f'[+] Shuffling...')
    indices = range(len(compiled))
    combined = list(zip(compiled, indices))
    subcombined = combined[1:]
    random.shuffle(subcombined)
    combined[1:] = subcombined
    compiled, indices = zip(*combined)
    compiled = list(compiled)
    indices = list(indices)
    print()

    # Correct the fallthrough destinations
    print(f'[+] Correcting fallthroughs...')
    reordering = {}
    for new, old in enumerate(indices):
        reordering[old] = new
    for old, new in reordering.items():
        packed = compiled[new]

        # The last instruction will not have a valid fallthrough
        if old+1 in reordering:
            dst = reordering[old+1]
            fallthrough = sum(len(s) for s in compiled[:dst])
        else:
            fallthough = 0xDC261B
        fallthrough ^= 0xDC2606
        packed = packed[:-3] + fallthrough.to_bytes(3, 'little')
        compiled[new] = packed

        print(f'{old:4d} --> {new:4d} ({packed})')
    print()

    # Adjust the label references according to the shuffle
    print(f'[+] Correcting label reference indices...')
    for lbl in labelRefs:
        newRefs = []
        for i in labelRefs[lbl]:
            newRefs.append(reordering[i])
        print(f'[*] {labelRefs[lbl]} --> {newRefs}')
        labelRefs[lbl] = newRefs
    print()

    # Find the new offsets of the labels
    print(f'[+] Correcting label indices...')
    for lbl in labels:
        if labels[lbl] in reordering:
            print(f'[*] ({lbl}) {labels[lbl]} --> {reordering[labels[lbl]]}', )
            labels[lbl] = reordering[labels[lbl]]
    print()

    # Correct the label references to accurately reflect the label offset
    print(f'[+] Applying label offsets to references')
    for lbl in labelRefs:
        if lbl not in labels:
            print(f'\033[1m\033[31mERROR: \033[37mUndefined label ({lbl})')
            return

        print(f'[*] Resolving {len(labelRefs[lbl])} references to {lbl} @ +{labels[lbl]}')
        offsetRaw = sum(len(s) for s in compiled[:labels[lbl]])
        offset = offsetRaw.to_bytes(3, 'big')
        for ref in labelRefs[lbl]:
            compiled[ref] = compiled[ref].replace(b'\xb0\x00\xb5', offset)
    print()

    # Calculate the obfuscated length of the virtual code
    print(f'[+] Obfuscating the compiled length...')
    ans = sum(len(s) for s in compiled)
    kElf = 0x464c457f    
    kLo = random.getrandbits(32)
    kMix = ans ^ kElf
    kHi = kMix ^ kLo
    kHi = ROR32(kHi, 13)
    kLo = ROL32(kLo, 27)
    cipherLen = kHi.to_bytes(4, byteorder='big', signed=False) + \
                kLo.to_bytes(4, byteorder='big', signed=False)
    print()

    # Print the raw byte code
    # for insn in compiled:
        # for b in insn:
            # sys.stdout.write(f'{b:02x} ')
    # print('\n')

    # Print the encrypted length
    # for x in cipherLen:
        # sys.stdout.write(f'{x:02x} ')
    # print('\n')

    # Write the binary file
    with open('../VM/woede.bin', 'wb') as f:
        for insn in compiled:
            f.write(bytearray(insn))
        f.write(cipherLen)

    # Check for length problems
    if ans > 0xFFFFFF:
        print('WARNING: Resulting code exceeds 0xFFFFFF')


if __name__ == '__main__':
    main()
