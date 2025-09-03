#include <stdint.h>
#include "stack.h"


#define WEEPER_VMEM_LOCAL_LO    0x000000
#define WEEPER_VMEM_LOCAL_HI    0x00FFFF
#define WEEPER_VMEM_STACK_LO    0x010000
#define WEEPER_VMEM_STACK_HI    0x01FFFF
#define WEEPER_VMEM_RESERVED_LO 0x020000
#define WEEPER_VMEM_RESERVED_HI 0x02FFFF
#define WEEPER_VMEM_SCRATCH_LO  0x030000
#define WEEPER_VMEM_SCRATCH_HI  0x0FFFFF
#define WEEPER_VMEM_SIZE        0x0FFFFF + 1

#define WEEPER_ERR_STACK    0xEF32

#define VREGS_LEN sizeof(uint32_t) * 16


typedef enum {
    VREG_P0, VREG_P1, VREG_P2, VREG_P3,
    VREG_R0, VREG_R1, VREG_R2, VREG_R3, VREG_R4, VREG_R5,
    VREG_F0, VREG_F1, VREG_F2, VREG_F3,
    VREG_C0, VREG_C1
} WeeperRegister;


typedef enum {
    WEEPER_OPC_XOR_0 = 0,
    WEEPER_OPC_XOR_1 = 1,
    WEEPER_OPC_XOR_2 = 2,
    WEEPER_OPC_XOR_3 = 3,
} WeeperOpcodeXor_t;


typedef enum {
    WEEPER_OP_T_NONE = 0,
    WEEPER_OP_T_REG = 1,
    WEEPER_OP_T_MEM = 2,
    WEEPER_OP_T_IMM = 3,
} WeeperOp_t;


typedef enum {
    WEEPER_OP_V_NULL = 0,
    WEEPER_OP_V_BYTE = 1,
    WEEPER_OP_V_SHORT = 2,
    WEEPER_OP_V_INT = 3,
} WeeperOp_v;


typedef enum {
    WOC_MOV, WOC_LEA, WOC_PUT, WOC_GET, // Data movement
    WOC_ADD, WOC_SUB, WOC_MUL, WOC_DIV, WOC_MOD, // Arithmetic
    WOC_CMP, WOC_JMP, WOC_CALL, WOC_RET, WOC_EXIT, // Control flow
    WOC_AND, WOC_OR, WOC_XOR, WOC_SHL, WOC_SHR, WOC_NOT, // Bitwise
    WOC_SYSCALL, WOC_SWAP, WOC_REV, WOC_PACKHI, WOC_PACKLO, WOC_ROL, WOC_ROR, WOC_FORK // Special
} WeeperOpcode;


typedef enum {
    WEEPER_FLAG_EQ  = 0b0001,
    WEEPER_FLAG_LT  = 0b0010,
    WEEPER_FLAG_GT  = 0b0100,
    WEEPER_FLAG_LTE = WEEPER_FLAG_LT | WEEPER_FLAG_EQ,
    WEEPER_FLAG_GTE = WEEPER_FLAG_GT | WEEPER_FLAG_EQ,
    WEEPER_FLAG_NEQ = WEEPER_FLAG_LT | WEEPER_FLAG_GT,
    WEEPER_FLAG_ERR = 0b1000,
} WeeperFlag;


typedef enum {
    WEEPER_SYSCALL_SLEEP    = 0x10,
    WEEPER_SYSCALL_GETPID   = 0x11,
    WEEPER_SYSCALL_GETPPID  = 0x12,
    WEEPER_SYSCALL_KILL     = 0x13,
    WEEPER_SYSCALL_TOD      = 0x14,
    WEEPER_SYSCALL_OPEN     = 0x20,
    WEEPER_SYSCALL_READ     = 0x21,
    WEEPER_SYSCALL_WRITE    = 0x22,
    WEEPER_SYSCALL_CLOSE    = 0x23,
    WEEPER_SYSCALL_LSEEK    = 0x24,
    WEEPER_SYSCALL_NOTINIT  = 0x30,
    WEEPER_SYSCALL_NOTADD   = 0x31,
    WEEPER_SYSCALL_NOTRM    = 0x32,
    WEEPER_SYSCALL_FUTEX    = 0x33,
} WeeperSyscall_t;


typedef struct __attribute__((__packed__)) {
    uint8_t opcode_l;
    uint8_t op1_t : 2;
    uint8_t op2_v : 2;
    uint8_t op1_l : 2;
    uint8_t op2_t : 2;
    uint8_t opcode_r;
    uint8_t op1_v : 2;
    WeeperOpcodeXor_t xor_t : 2;
    uint8_t op2_l : 2;
}  WeeperInsnHdr;


typedef struct __attribute__((__packed__)) {
    WeeperOp_t t : 2;
    WeeperOp_v v : 2;
    uint8_t l : 4;
    uint8_t *data;
} WeeperOperand;


typedef struct {
    uint32_t *vregs;
    Stack *callStack;
    WeeperFlag vflag;
} WeeperContext;


/**
 * Emulate code after loading from _start
 */
void emulate(uint32_t vip);
