# WeeperVM

<p align="center"> <img src="https://github.com/A-Benlolo/WeeperVM/blob/main/img/prompt.png?raw=true"/> </p>

A capture the flag (CTF) obfuscated with a custom, highly capable virtual machine. Originally shared on [CrackMyApp](https://crackmy.app/crackmes/weepervm-level-2-6068) and [crackmes.one](https://crackmes.one/crackme/6807f6ab8f555589f3530e64). After nearly 5 months of standing unsolved, the key was finally cracked by [darbozno](https://x64.ooo/posts/2025-09-14-reverse-engineering-vm-weeper2/).

There are several components developed for this CTF, but I did not initially intend to share the source code. So, lack of "best practice" is to be expected, but it works out in the end.


## Virtual Machine

This is the host code that runs the virtualized code.

<p align="center"> <img src="https://github.com/A-Benlolo/WeeperVM/blob/main/img/emulate_cfg.png?raw=true"/> </p>

Special care was used to ensure absolutely no dependencies are present, even libc. Once compiled, it should be capable of running on any system with a matching architecture.

### Code Location

The virtual code that the VM will run must be appended to the VM executable. The tail of the virtual code describes its length, allowing it to be extracted and mapped into memory.

Light anti-analysis is implemented by requiring the virtual code be deciphered during loading, but that is not the focus of the VM.

### Memory Space

There is 1 MB of virtual memory with the following memory reservations.

| Start address | End Address | Purpose |
|---|---|--- |
| 0x000000 | 0x00FFFF | Local variables |
| 0x010000 | 0x01FFFF | Stack variables* |
| 0x020000 | 0x02FFFF | Reserved for future use |

\* The virtual architecture does not have a stack in the traditional sense, but the memory reservation is left to complicate the reverse engineering process.

Instead, a unique key is calculated per local variable, which would usually go on the stack. This key is used as an offset into the local variable memory space. In other words, a dictionary is used in place of a stack. So, instead of **PUSH** and **POP** you have **PUT** and **GET**.

This does have the potential of collisions in especially large virtualized code, but recompiling has solved this problem in the past due to the instruction shuffling (discussed later).

### Registers

There are 16, 32-bit virtual registers.

The following categories make developing virtual code easier to organize. Note that registers are intended to be volatile and non-volatile between function calls, but this task is left to the developer of virtual code to implement.

| Register Names | Purpose | Volatile? |
|---|---|---|
| P0 - P4 | Persistant data | No
| R0 - R5 | General purpoes | Yes
| F0 - F4 | Function parameters | No |
| C0 - C1 | Counters | No |

## Virtualized Code

Commented and uncommented code are available.

The commented code contains the solution to the challenge as well as a high-level solution process.

### Formal Syntax

The virtual code is x86 assembly-like with some quality of life modifications, matching the following is a BNF syntax definition. Note that labels begin with "FUN\_" or "LAB\_" by convention, but this is not required by the grammar.

```
code ::= <constant> ... <expr> ...

constant ::= {<constname> ?decimal-octal-hex-or-bin?}

constname ::= ?ascii?

expr ::=
  | <label>
  | <instruction>

label ::= <?ascii?>

instruction ::=
  | <opcode>
  | <opcode> <operand>
  | <opcode> <operand> <operand>
  | <opcode> <operand> <flag>

opcode ::=
  | MOV | LEA | PUT | GET
  | ADD | SUB | MUL | DIV | MOD
  | CMP | JMP | CALL | RET | EXIT
  | AND | OR | XOR | SHL | SHR | NOT
  | SYSCALL | SWAP | REV | PACKHI | PACKLO | ROL | ROR | FORK

flag ::=
  | EQ | LT | GT | LTE | GTE | NEQ | ERR

operand ::=
  | <register>
  | <memory>
  | <constname>
  | ?decimal-octal-hex-or-bin?

register ::=
  | <regid>
  | <regid><sz>

regid ::=
  | P0 | P1 | P2 | P3
  | R0 | R1 | R2 | R3 | R4 | R5
  | F0 | F1 | F2 | F4
  | C0 | C1

memory ::=
  | [<constname>]
  | [?decimal-octal-hex-or-bin?]
  | [<register>]
  | [<register>+<register>]
  | [<register>+<constname>]
  | [<register>+?decimal-octal-hex-or-bin?]
  | [<constname>]<sz>
  | [?decimal-octal-hex-or-bin?]<sz>
  | [<register>]<sz>
  | [<register>+<register>]<sz>
  | [<register>+<constname>]<sz>
  | [<register>+?decimal-octal-hex-or-bin?]<sz>

sz ::=
  | .b | .s | .i

```

### Compiler

A compiler (really a transpiler) is used to convert the ASCII virtualized code into a binary format.

It will perform sanity checks on missing or duplicate labels or global constants. Validation of opcodes and operaters will also be performed.

There are three points of obfuscation in the compiler.

- Each instruction is uniquely encrypted using one of four XOR ciphers. This adds entropy to the opcode and operand lengths.
- The last 3 bytes of the instruction describe the offset into the virtual code for the fallthrough instruction, allowing the compiled instruction order to be shuffled. This requires a reverse engineer to unshuffle the instructions before analyzing the virtual code.
- All virtual code is encrypted using the first four bytes from the ELF header.


## Constraint Generator

One intention of this CTF was to require the use of an SMT solver. For this, a helper script was written to generate a list of constraints that are only satisfiable for a unique 32-bit integer.

The constraints are lifted from Z3 into WeeperVM assembly and printed to a text file. You can see the results at the end of the provided WeeperVM assembly code.

The actual list of numbers that the code was generated for will not be shared here, since that solves a significant portion of the challenge.
