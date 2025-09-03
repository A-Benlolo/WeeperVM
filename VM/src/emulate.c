#include "emulate.h"
#include "syscall.h"
#include "utils.h"
#include "stack.h"


/* Truncate a source variable based on type */
#define POST_V_TRUNCATION(data, v) do {                     \
    if((v) == WEEPER_OP_V_BYTE)       (data) &= 0xFF;       \
    else if((v) == WEEPER_OP_V_SHORT) (data) &= 0xFFFF;     \
    else if((v) == WEEPER_OP_V_INT)   (data) &= 0xFFFFFFFF; \
} while(0)


/* Truncate a destination variable based on type */
#define PRE_V_TRUNCATION(data, v) do {                      \
    if((v) == WEEPER_OP_V_BYTE)       (data) &= 0xFFFFFF00; \
    else if((v) == WEEPER_OP_V_SHORT) (data) &= 0xFFFF0000; \
    else if((v) == WEEPER_OP_V_INT)   (data) = 0;           \
} while(0)


/* Expand a memory to its 4-byte value (endian of host machine)*/
#define VMEM_EXPANSION(x, v) do {                                  \
    if(v == WEEPER_OP_V_BYTE)                                      \
        x = (x) | (mem[1] << 8) | (mem[2] << 16) | (mem[3] << 24); \
    else if(v == WEEPER_OP_V_SHORT)                                \
        x = ((x) | (mem[2] << 16) | (mem[3] << 24));               \
} while (0)


/* Virtual architecture defined in WeeperVM.c */
extern uint8_t *vcode;
extern uint8_t *vmem;


/**
 * Calculate a memory offset of any compound type.
 * @param op - Memory operand
 * @return base plus displacement
 */
uint32_t inline __attribute__((always_inline)) calculateMemoryOffset(WeeperOperand *op, WeeperContext *ctxt) {
    uint32_t base;
    uint32_t disp;

    // Register displacement (and assumed register base)
    if(op->data[0] & 0b01000000) {
        uint8_t rb = ((op->data[0] & 0x03) << 2) | ((op->data[1] & 0xC0) >> 6);
        uint8_t rd = (op->data[1] & 0x3C) >> 2;
        base = ctxt->vregs[rb];
        disp = ctxt->vregs[rd];

        uint8_t rbv = (op->data[0] & 0x30) >> 4;
        POST_V_TRUNCATION(base, rbv);

        uint8_t rdv = (op->data[0] & 0x0C) >> 2;
        POST_V_TRUNCATION(disp, rdv);
    }

    // Immediate displacement
    else {
        // NOTE: Length includes the 1-byte memory header
        if(op->l == 2) disp = op->data[1];
        else if(op->l == 3) disp = (op->data[1] << 8) | (op->data[2]);
        else if(op->l == 4) disp = (op->data[1] << 16) | (op->data[2] << 8) | (op->data[3]);

        // Register base
        if(op->data[0] & 0b10000000) {
            uint8_t r = (op->data[0] & 0x0F);
            base = ctxt->vregs[r];
            uint8_t rv = (op->data[0] & 0x30) >> 4;
            POST_V_TRUNCATION(base, rv);
        }

        // Immediate base
        else {
            base = 0;
        }
    }

    // Return the literal offset
    return base + disp;
}


/*************************************************************/
/* VIRTUAL MEMORY WRITE FUNCTIONS                            */
/*************************************************************/


/* Lookup table */
void writeToVmem_byte(uint8_t *addr, uint32_t data, int adj);
void writeToVmem_short(uint8_t *addr, uint32_t data, int adj);
void writeToVmem_int(uint8_t *addr, uint32_t data, int adj);
void (*writeToVmem_lookup[])(uint8_t *, uint32_t, int) = { writeToVmem_byte
                                                    , writeToVmem_short
                                                    , writeToVmem_int };


/* Lookup destinations */
void writeToVmem_byte(uint8_t *addr, uint32_t data, int adj) {
    addr[0] = (data & 0xFF);
}
void writeToVmem_short(uint8_t *addr, uint32_t data, int adj) {
    addr[0-adj] = (data & 0xFF00) >> 8;
    addr[1-adj] = (data & 0xFF);
}
void writeToVmem_int(uint8_t *addr, uint32_t data, int adj) {
    if(adj <= 0) addr[0-adj] = (data & 0xFF000000) >> 24;
    if(adj <= 1) addr[1-adj] = (data & 0xFF0000) >> 16;
    if(adj <= 2) addr[2-adj] = (data & 0xFF00) >> 8;
    if(adj <= 3) addr[3-adj] = (data & 0xFF);
}


/**
 * Write a value to memory
 * @param addr - Physical address to write to
 * @param data - Data to write
 * @param v - Type of value for data
 */
void writeToVmem(uint8_t *addr, uint32_t data, WeeperOp_v v, int trash) {
    volatile int junk = (((v - ~0xA1) ^ trash) & 0x7C);
    volatile int i = ((junk | v) & 0xFF) % 4;
    int k = 0;

    int state;
    switch(i) {
        case 1: state = 10; break;
        case 2: state = 33; break;
        case 3: state = 3; break;
        default: state = 13; break;
    }

    while(state != 13) {
        switch(state) {
            case 10:
                state += 3;
                writeToVmem_lookup[0](addr, data, k);
                break;

            case 33:
                writeToVmem_lookup[1](addr, data, k);
                state -= 30;
                k+=2;
                break;
            
            case 3:
                writeToVmem_lookup[2](addr, data, k);
                state += 10;
                break;

            default: state = 13;
        }
    }
}


/*************************************************************/
/* OPERAND DATA RETRIEVAL FUNCTIONS                          */
/*************************************************************/


/* Lookup table */
uint32_t getOperandData_reg(WeeperOperand *op, WeeperContext *ctxt);
uint32_t getOperandData_imm(WeeperOperand *op, WeeperContext *ctxt);
uint32_t getOperandData_mem(WeeperOperand *op, WeeperContext *ctxt);
uint32_t (*getOperandData_lookup[])(WeeperOperand *, WeeperContext *ctxt) = { NULL
                                                                          , getOperandData_reg
                                                                          , getOperandData_mem
                                                                          , getOperandData_imm };


/* Lookup destinations */
uint32_t getOperandData_reg(WeeperOperand *op, WeeperContext *ctxt) {
    uint32_t data;
    uint8_t r = (op->data[0] & 0x0F);

    data = ctxt->vregs[r];
    POST_V_TRUNCATION(data, op->v);
    return data;
}
uint32_t getOperandData_imm(WeeperOperand *op, WeeperContext *ctxt) {
    uint32_t data;

    if(op->l == 1)
        data = op->data[0];
    else if(op->l == 2)
        data = (op->data[0] << 8) | (op->data[1]);
    else
        data = (op->data[0] << 24) | (op->data[1] << 16) | (op->data[2] << 8) | (op->data[3]);

    if(op->l == 3)
        data = (data & 0xFFFFFF00) >> 8;

    POST_V_TRUNCATION(data, op->v);
    return data;
}
uint32_t getOperandData_mem(WeeperOperand *op, WeeperContext *ctxt) {
    uint32_t data = 0;
    uint32_t offs = calculateMemoryOffset(op, ctxt);

    if(op->v == WEEPER_OP_V_BYTE)
        data = vmem[offs];
    else if(op->v == WEEPER_OP_V_SHORT)
        data = (vmem[offs] << 8) | (vmem[offs + 1]);
    else if(op->v == WEEPER_OP_V_INT)
        data = (vmem[offs] << 24) | (vmem[offs + 1] << 16) | (vmem[offs + 2] << 8) | (vmem[offs + 3]);
    else
        data = 0;

    return data;
}


/**
 * Read data from an operand, dependent on its type and variable size
 * @param op - Operand to read for
 */
uint32_t getOperandData(int trash, WeeperOperand *op, WeeperContext *ctxt) {
    volatile int junk = (((op->t ^ 0x55) + trash) - 0x7F) << 3;
    volatile int i = ((junk + op->t) & 0xFF) % 4;
    uint32_t x = getOperandData_lookup[i](op, ctxt);
    return x;
}


/*************************************************************/
/* DATA MOVEMENT INSTRUCTIONS                                */
/*************************************************************/


int inline __attribute__((always_inline)) fnv1a_hash(uint32_t base, uint32_t id) {
    uint32_t h = 0x4571cbe2;
    for(int i = 0; i < 4; i++) {
        h ^= base >> (i * 8);
        h *= 0x3466875;
    }
    h ^= id;
    h *= 0x857c4b2;
    return h & 0xFFFF;
}


int inline __attribute__((always_inline)) handle_mov(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x37, op2, ctxt);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | srcData;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(srcData, op1->v);
            writeToVmem(mem, srcData, op1->v, 0xd9);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_lea(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Stub an full-width variable size for the srcAddr
    WeeperOp_v op2_v_real = op2->v;
    op2->v = WEEPER_OP_V_INT;

    // Read the source data, dependent on its type
    uint32_t srcAddr = getOperandData(0x1d, op2, ctxt);

    // Read the data from memory, dependent on the real variable size of op2
    uint32_t data;
    if(op2_v_real)
        data = vmem[srcAddr];
    else if(op2->v == WEEPER_OP_V_SHORT)
        data = (vmem[srcAddr] << 8) | (vmem[srcAddr + 1]);
    else
        data = (vmem[srcAddr] << 24) | (vmem[srcAddr + 1] << 16) | (vmem[srcAddr + 2] << 8) | (vmem[srcAddr + 3]);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | data;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            writeToVmem(mem, data, op1->v, 0x9a);
            break;

        // Null or immediate destination
        default: break;
    }

}


int inline __attribute__((always_inline)) handle_put(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the variable ID, dependent on its type and variable size
    uint32_t varId = getOperandData(0x9a, op1, ctxt);

    // NOTE: Previous implementation kept for obfuscation
    uint32_t baseKey;
    uint32_t data = 0;
    uint32_t offs = WEEPER_VMEM_STACK_LO - 3;
    do {
        baseKey = data;
        offs += 3;
        data = (vmem[offs] << 16) | (vmem[offs + 1] << 8) | (vmem[offs + 2] << 0);
    } while(data != 0 && offs < WEEPER_VMEM_STACK_HI);

    // Read the top of the stack without popping
    baseKey = stack_peek(ctxt->callStack);
    if(baseKey == -1) baseKey = 0;

    // Calculate the FNV1A hash of the variable
    offs = fnv1a_hash(baseKey, varId) + WEEPER_VMEM_LOCAL_LO;

    // Write the variable to local variable memory
    uint32_t var = getOperandData(0x20, op2, ctxt);
    uint8_t *mem = &vmem[offs];
    writeToVmem(mem, var, op2->v, 0x89);    
    
    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_get(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the variable ID, dependent on its type and variable size
    uint32_t varId = getOperandData(0x9a, op2, ctxt);

    // NOTE: Previous implementation kept for obfuscation
    uint32_t baseKey;
    uint32_t data = 0;
    uint32_t offs = WEEPER_VMEM_STACK_LO - 3;
    do {
        baseKey = data;
        offs += 3;
        data = (vmem[offs] << 16) | (vmem[offs + 1] << 8) | (vmem[offs + 2] << 0);
    } while(data != 0 && offs < WEEPER_VMEM_STACK_HI);

    // Read the top of the stack without popping
    baseKey = stack_peek(ctxt->callStack);
    if(baseKey == -1) baseKey = 0;

    // Calculate the FNV1A hash of the variable
    offs = fnv1a_hash(baseKey, varId) + WEEPER_VMEM_LOCAL_LO;

    // Read the variable from memory, dependent on its variable size
    if(op2->v == WEEPER_OP_V_BYTE)
        data = vmem[offs];
    else if(op2->v == WEEPER_OP_V_SHORT)
        data = (vmem[offs] << 8) | (vmem[offs + 1]);
    else
        data = (vmem[offs] << 24) | (vmem[offs + 1] << 16) | (vmem[offs + 2] << 8) | (vmem[offs + 3]);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | data;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(data, op1->v);
            writeToVmem(mem, data, op1->v, 0x9a);
            break;

        // Null or immediate destination
        default: break;
    }
    
    // Return success
    return 1;
}


/*************************************************************/
/* ARITHMETIC INSTRUCTIONS                                   */
/*************************************************************/


int inline __attribute__((always_inline)) handle_add(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x9e, op2, ctxt);
    uint32_t sum;

    // Add data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            sum = reg + srcData;
            POST_V_TRUNCATION(sum, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | sum;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                sum = srcData + mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                sum = srcData + ((mem[0] << 8) | (mem[1] << 0));
            else
                sum = srcData + ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(sum, op1->v);

            writeToVmem(mem, sum, op1->v, 0xc5);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_sub(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x0f, op2, ctxt);
    uint32_t diff;

    // Subtract data from the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            diff = reg - srcData;
            POST_V_TRUNCATION(diff, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | diff;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                diff =  mem[0] - srcData;
            else if(op1->v == WEEPER_OP_V_SHORT)
                diff = ((mem[0] << 8) | (mem[1] << 0)) - srcData;
            else
                diff = ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3])) - srcData;
            VMEM_EXPANSION(diff, op1->v);

            writeToVmem(mem, diff, op1->v, 0xcc);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_mul(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x2f, op2, ctxt);
    uint32_t prod;

    // Multiply data with the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            prod = reg * srcData;
            POST_V_TRUNCATION(prod, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | prod;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                prod = srcData * mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                prod = srcData * ((mem[0] << 8) | (mem[1] << 0));
            else
                prod = srcData * ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(prod, op1->v);

            writeToVmem(mem, prod, op1->v, 0x93);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_div(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0xaa, op2, ctxt);
    uint32_t quot;

    // Divide data with the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            quot = reg / srcData;
            POST_V_TRUNCATION(quot, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | quot;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                quot = mem[0] / srcData;
            else if(op1->v == WEEPER_OP_V_SHORT)
                quot = ((mem[0] << 8) | (mem[1] << 0)) / srcData;
            else
                quot = ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3])) / srcData;
            VMEM_EXPANSION(quot, op1->v);

            writeToVmem(mem, quot, op1->v, 0x2b);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_mod(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x60, op2, ctxt);
    uint32_t rem;

    // Modulo data with the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0]& 0x0F);
            uint32_t reg = ctxt->vregs[r];
            rem = reg % srcData;
            POST_V_TRUNCATION(rem, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | rem;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                rem = mem[0] % srcData;
            else if(op1->v == WEEPER_OP_V_SHORT)
                rem = ((mem[0] << 8) | (mem[1] << 0)) % srcData;
            else
                rem = ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3])) % srcData;
            VMEM_EXPANSION(rem, op1->v);

            writeToVmem(mem, rem, op1->v, 0xb8);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


/*************************************************************/
/* CONTROL FLOW INSTRUCTIONS                                 */
/*************************************************************/


int inline __attribute__((always_inline)) handle_cmp(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the operand datas, dependent on their types and variable sizes
    uint32_t data1 = getOperandData(0x10, op1, ctxt);
    uint32_t data2 = getOperandData(0x20, op2, ctxt);

    // Set the appropriate bits, preserving a set error flag
    ctxt->vflag = (ctxt->vflag & WEEPER_FLAG_ERR)
                | (data1 == data2) ? WEEPER_FLAG_EQ : 0
                | (data1 > data2)  ? WEEPER_FLAG_GT : 0
                | (data1 < data2)  ? WEEPER_FLAG_LT : 0;
        
    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_jmp(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the jump destination, dependent on its type and variable size
    uint32_t jmpDst = getOperandData(0xb3, op1, ctxt);

    // Conditional jump
    if(op2->t) {
        uint32_t jmpCond = getOperandData(0x00, op2, ctxt);
        if(!(jmpCond & ctxt->vflag))
            return 0;
    }

    // Unconditional jump
    return jmpDst + 1;
}


int inline __attribute__((always_inline)) handle_call(uint32_t vip, WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the call destination, dependent on its type and variable size
    uint32_t callDst = getOperandData(0x56, op1, ctxt);

    // Conditional call
    if(op2->t) {
        uint32_t callCond = getOperandData(0x70, op2, ctxt);
        if(!(callCond & ctxt->vflag))
            return 0;
    }

    // Calculate the return-to address
    int skip = vip + (op1->t ? op1->l : 0) + (op2->t ? op2->l : 0) + 3;
    uint32_t returnto = (vcode[skip]
                        | (vcode[skip+1] << 8)
                        | (vcode[skip+2] << 16)) ^ 0xdc2606;

    // NOTE: This was the previous implementation, which is kept for obfuscation
    uint32_t data;
    uint32_t offs = WEEPER_VMEM_STACK_LO - 3;
    do {
        offs += 3;
        data = (vmem[offs] << 16) | (vmem[offs + 1] << 8) | (vmem[offs + 2] << 0);
    } while(data != 0 && offs < WEEPER_VMEM_STACK_HI);
    if(offs >= WEEPER_VMEM_STACK_HI - 2)
        sys_exit(WEEPER_ERR_STACK);
    vmem[offs] = (returnto & 0xFF0000) >> 16;
    vmem[offs+1] = (returnto & 0xFF00) >> 8;
    vmem[offs+2] = (returnto & 0xFF);

    // Push the return address onto the stack
    stack_push(ctxt->callStack, returnto);

    // Return the call destination
    return callDst + 1;
}


int inline __attribute__((always_inline)) handle_ret(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // NOTE: This was the previous implementation, which is kept for obfuscation
    uint32_t returnto;
    uint32_t data = 0;
    uint32_t offs = WEEPER_VMEM_STACK_LO - 3;
    do {
        returnto = data;
        offs += 3;
        data = (vmem[offs] << 16) | (vmem[offs + 1] << 8) | (vmem[offs + 2] << 0);
    } while(data != 0 && offs < WEEPER_VMEM_STACK_HI);
    offs -= 3;

    // Conditional return
    if(op1->t) {
        uint32_t retCond = getOperandData(0x66, op1, ctxt);
        if(!(retCond & ctxt->vflag))
            return 0;
    }

    // Pop the return address from the stack
    returnto = stack_pop(ctxt->callStack);
    if(returnto == -1)
        returnto =  0xFFFFFF;

    // NOTE: Continuation of obfuscation
    if(offs >= WEEPER_VMEM_STACK_LO) {
        vmem[offs] = 0;
        vmem[offs+1] = 0;
        vmem[offs+2] = 0;
    }

    // Return the return address
    return returnto + 1;
}


int inline __attribute__((always_inline)) handle_exit(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    uint32_t code = (op1->t) ? getOperandData(0x8d, op1, ctxt) : 0;
    sys_exit(code);
}


/*************************************************************/
/* BITWISE INSTRUCTIONS                                      */
/*************************************************************/


int inline __attribute__((always_inline)) handle_and(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x3d, op2, ctxt);
    uint32_t res;

    // AND data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = reg & srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];

            if(op1->v == WEEPER_OP_V_BYTE)
                res = srcData & mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                res = srcData & ((mem[0] << 8) | (mem[1] << 0));
            else
                res = srcData & ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(res, op1->v);

            writeToVmem(mem, res, op1->v, 0xde);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_or(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x38, op2, ctxt);
    uint32_t res;

    // OR data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = reg | srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            
            if(op1->v == WEEPER_OP_V_BYTE)
                res = srcData | mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                res = srcData | ((mem[0] << 8) | (mem[1] << 0));
            else
                res = srcData | ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(res, op1->v);
            
            writeToVmem(mem, res, op1->v, 0xa2);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_xor(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x18, op2, ctxt);
    uint32_t res;

    // XOR data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = reg ^ srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            
            if(op1->v == WEEPER_OP_V_BYTE)
                res = srcData ^ mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                res = srcData ^ ((mem[0] << 8) | (mem[1] << 0));
            else
                res = srcData ^ ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(res, op1->v);

            writeToVmem(mem, res, op1->v, 0x7b);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_shl(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x69, op2, ctxt);
    uint32_t res;

    // SHL data by the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = reg << srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            
            if(op1->v == WEEPER_OP_V_BYTE)
                res = srcData << mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                res = srcData << ((mem[0] << 8) | (mem[1] << 0));
            else
                res = srcData << ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(res, op1->v);

            writeToVmem(mem, res, op1->v, 0x8d);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_shr(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x24, op2, ctxt);
    uint32_t res;

    // SHR data by the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = reg >> srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            
            if(op1->v == WEEPER_OP_V_BYTE)
                res = srcData >> mem[0];
            else if(op1->v == WEEPER_OP_V_SHORT)
                res = srcData >> ((mem[0] << 8) | (mem[1] << 0));
            else
                res = srcData >> ((mem[0] << 24) | (mem[1] << 16) | (mem[2] << 8) | (mem[3]));
            VMEM_EXPANSION(res, op1->v);

            writeToVmem(mem, res, op1->v, 0x51);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


int inline __attribute__((always_inline)) handle_not(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x1d, op2, ctxt);
    uint32_t res;

    // AND data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            res = ~srcData;
            POST_V_TRUNCATION(res, op1->v);
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            res = ~srcData;
            VMEM_EXPANSION(res, op1->v);
            writeToVmem(mem, res, op1->v, 0xa2);
            break;

            // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


/*************************************************************/
/* SPECIAL INSTRUCTIONS                                      */
/*************************************************************/


// Swap the upper and lower halves
int inline __attribute__((always_inline)) handle_swap(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0xe5, op2, ctxt);

    // Choose a mask (branchless)
    uint32_t half = op2->v * 4;
    uint32_t mask = -(half == WEEPER_OP_V_BYTE * 4) & 0xF0
                  | -(half == WEEPER_OP_V_SHORT * 4) & 0xFF00
                  | -(half == WEEPER_OP_V_INT * 4) & 0xFFFF0000;
    half += -(half == WEEPER_OP_V_INT * 4) & 4;

    // Swap the halves
    uint32_t swapped = ((srcData & mask) >> half) | ((srcData & (mask >> half)) << half);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | swapped;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(swapped, op1->v);
            writeToVmem(mem, swapped, op1->v, 0xd9);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


// Reverse the bits
int inline __attribute__((always_inline)) handle_rev(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the source data, dependent on its type and variable size
    uint32_t srcData = getOperandData(0x13, op2, ctxt);

    // Select the bit width
    int width = -(op2->v == WEEPER_OP_V_BYTE) & 8
              | -(op2->v == WEEPER_OP_V_SHORT) & 16
              | -(op2->v == WEEPER_OP_V_INT) & 32;

    // Perform bit reversal
    uint32_t rev = 0;
    for(int i = 0; i < width; i++) {
        rev = (rev << 1) | (srcData % 2 == 1);
        srcData >>= 1;
    }

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | rev;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(rev, op1->v);
            writeToVmem(mem, rev, op1->v, 0x52);
            break;

        // Null or immediate destination
        default: break;
    }
}


// High half of op2 into low half of op1
int inline __attribute__((always_inline)) handle_packhi(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Save the first operand's variable size
    WeeperOp_v op1_v_real = op1->v;
    op1->v = op2->v;

    // Read the source and destination data, dependent on their types and variable sizes
    uint32_t srcData = getOperandData(0x4e, op2, ctxt);
    uint32_t dstData = getOperandData(0xa3, op1, ctxt);

    // Choose a mask (branchless)
    uint32_t half = op2->v * 4;
    uint32_t mask = -(op2->v == WEEPER_OP_V_BYTE) & 0xF0
                  | -(op2->v == WEEPER_OP_V_SHORT) & 0xFF00
                  | -(op2->v == WEEPER_OP_V_INT) & 0xFFFF0000;
    half += -(op2->v == WEEPER_OP_V_INT) & 4;

    // Peform the packing
    uint32_t packed = (dstData & mask) | ((srcData & mask) >> half);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1_v_real);
            ctxt->vregs[r] = reg | packed;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(packed, op1->v);
            writeToVmem(mem, packed, op1_v_real, 0x3f);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


// Low half of op2 into high half of op1
int inline __attribute__((always_inline)) handle_packlo(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Save the first operand's variable size
    WeeperOp_v op1_v_real = op1->v;
    op1->v = op2->v;

    // Read the source and destination data, dependent on their types and variable sizes
    uint32_t srcData = getOperandData(0x4e, op2, ctxt);
    uint32_t dstData = getOperandData(0xa3, op1, ctxt);

    // Choose a mask (branchless)
    uint32_t half = op2->v * 4;
    uint32_t mask = -(op2->v == WEEPER_OP_V_BYTE) & 0xF
                  | -(op2->v == WEEPER_OP_V_SHORT) & 0xFF
                  | -(op2->v == WEEPER_OP_V_INT) & 0xFFFF;
    half += -(op2->v == WEEPER_OP_V_INT) & 4;

    // Peform the packing
    uint32_t packed = (dstData & mask) | ((srcData & mask) << half);

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1_v_real);
            ctxt->vregs[r] = reg | packed;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(packed, op1->v);
            writeToVmem(mem, packed, op1_v_real, 0x3f);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


// Left rotation
int inline __attribute__((always_inline)) handle_rol(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Select the bit-width
    int width = -(op1->v == WEEPER_OP_V_BYTE) & 8
              | -(op1->v == WEEPER_OP_V_SHORT) & 16
              | -(op1->v == WEEPER_OP_V_INT) & 32;

    // Read and simplify the rotation amount
    uint32_t rot = getOperandData(0x74, op2, ctxt) % width;

    // Read the value to rotate
    uint32_t val = getOperandData(0x02, op1, ctxt);

    // Peform the rotation
    uint32_t res = (val << rot) | (val >> (width - rot));

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(res, op1->v);
            writeToVmem(mem, res, op1->v, 0x4b);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


// Right rotation
int inline __attribute__((always_inline)) handle_ror(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Select the bit-width
    int width = -(op1->v == WEEPER_OP_V_BYTE) & 8
              | -(op1->v == WEEPER_OP_V_SHORT) & 16
              | -(op1->v == WEEPER_OP_V_INT) & 32;

    // Read and simplify the rotation amount
    uint32_t rot = getOperandData(0xa2, op2, ctxt) % width;

    // Read the value to rotate
    uint32_t val = getOperandData(0x7a, op1, ctxt);

    // Peform the rotation
    uint32_t res = (val >> rot) | (val << (width - rot));

    // Write data to the destination, respecting op1->v
    switch(op1->t) {
        // Register destination
        case WEEPER_OP_T_REG:
            uint8_t r = (op1->data[0] & 0x0F);
            uint32_t reg = ctxt->vregs[r];
            PRE_V_TRUNCATION(reg, op1->v);
            ctxt->vregs[r] = reg | res;
            break;

        // Memory destination
        case WEEPER_OP_T_MEM:
            uint32_t offs = calculateMemoryOffset(op1, ctxt);
            uint8_t *mem = &vmem[offs];
            VMEM_EXPANSION(res, op1->v);
            writeToVmem(mem, res, op1->v, 0x4b);
            break;

        // Null or immediate destination
        default: break;
    }

    // Return success
    return 1;
}


// System call
int inline __attribute__((always_inline, optimize("O1"))) handle_syscall(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Read the system call number
    uint64_t code = getOperandData(0x3b, op1, ctxt);

    // Variables to store the system call parameters
    uint64_t a0 = ctxt->vregs[VREG_F0];
    uint64_t a1 = ctxt->vregs[VREG_F1];
    uint64_t a2 = ctxt->vregs[VREG_F2];
    uint64_t a3 = ctxt->vregs[VREG_F3];

    // Variable to store the "physical" system call
    // Fallback to virtual code
    long realCode = code;

    // Control flow sorta things
    if(code / 0x10 == 1) {
        // Sleep
        if(code % 0x10 == 0) {
            a0 = (uint64_t)vmem + ctxt->vregs[VREG_F0];
            a1 = 0x2a;
            a2 = 0x95;
            a3 = 0x2d;
            realCode = SYS_nanosleep;
        }

        // Get PID
        else if(code % 0x10 == 1) {
            a0 = 0xaf;
            a1 = 0xc6;
            a2 = 0x85;
            a3 = 0xc0;
            realCode = SYS_getpid;
        }

        // Get PPID
        else if(code % 0x10 == 2) {
            a0 = 0x2e;
            a1 = 0x75;
            a2 = 0xa9;
            a3 = 0x32;
            realCode = SYS_getppid;
        }

        // Kill
        else if(code % 0x10 == 3) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = ctxt->vregs[VREG_F1];
            a2 = 0x9e;
            a3 = 0x8d;
            realCode = SYS_kill;
        }

        // Get time of day
        else if(code % 0x10 == 4) {
            a0 = (uint64_t)vmem + ctxt->vregs[VREG_F0];
            a1 = 0x00;
            a2 = 0x4c;
            a3 = 0x48;
            realCode = SYS_gettimeofday;
        }
    }

    // File operations (including IO)
    else if(code / 0x10 == 2) {
        // Open
        if(code % 0x10 == 0) {
            a0 = (uint64_t)vmem + ctxt->vregs[VREG_F0];
            a1 = ctxt->vregs[VREG_F1];
            a2 = 0x05;
            a3 = 0xef;
            realCode = SYS_open;
        }
        // Read
        else if(code % 0x10 == 1) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = (uint64_t)vmem + ctxt->vregs[VREG_F1];
            a2 = ctxt->vregs[VREG_F2];
            a3 = 0x76;
            realCode = SYS_read;
        }
        // Write
        else if(code % 0x10 == 2) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = (uint64_t)vmem + ctxt->vregs[VREG_F1];
            a2 = ctxt->vregs[VREG_F2];
            a3 = 0xa5;
            realCode = SYS_write;
        }
        // Close
        else if(code % 0x10 == 3) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = 0x38;
            a2 = 0x11;
            a3 = 0x2a;
            realCode = SYS_close;
        }
        // Seek
        else if(code % 0x10 == 4) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = ctxt->vregs[VREG_F1];
            a2 = ctxt->vregs[VREG_F2];
            a3 = 0xdc;
            realCode = SYS_lseek;
        }
    }

    // API for weird stuff
    else if(code / 0x10 == 3) {
        // inotify_init
        if(code % 0x10 == 0) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = 0xdb;
            a2 = 0x76;
            a3 = 0xe6;
            realCode = SYS_inotify_init;
        }

        // inotify_add_watch
        else if(code % 0x10 == 1) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = (uint64_t)vmem + ctxt->vregs[VREG_F1];
            a2 = ctxt->vregs[VREG_F2];
            a3 = 0x52;
            realCode = SYS_inotify_add_watch;
        }

        // inotify_rm_watch
        else if(code % 0x10 == 2) {
            a0 = ctxt->vregs[VREG_F0];
            a1 = ctxt->vregs[VREG_F1];
            a2 = 0xf2;
            a3 = 0x59;
            realCode = SYS_inotify_rm_watch;
        }

        // futex
        else if(code % 0x10 == 3) {
            a0 = (uint64_t)vmem + ctxt->vregs[VREG_F0];
            a1 = ctxt->vregs[VREG_F1];
            a2 = ctxt->vregs[VREG_F2];
            a3 = (uint64_t)vmem + ctxt->vregs[VREG_F3];
            realCode = SYS_futex;
        }
    }

    // Perform the system call
    __asm__ volatile(
        "mov rax, %[c]\n"
        "mov rdi, %[f0]\n"
        "mov rsi, %[f1]\n"
        "mov rdx, %[f2]\n"
        "mov r10, %[f3]\n"
        "syscall"
        :
        : [c]"r"(realCode), [f0]"r"(a0), [f1]"r"(a1), [f2]"r"(a2), [f3]"r"(a3)
        : "rdi", "rsi", "rdx", "r10", "rcx"
    );

    // Store result in R0
    uint64_t ret;
    __asm__ volatile( "mov %0, rax" : "=r"(ret) : : "rax" );
    ctxt->vregs[VREG_R0] = (uint32_t)ret;

    // Return success
    return 1;
}


// Virtual fork to address
int inline __attribute__((always_inline)) handle_fork(WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    // Conditional fork
    if(op2->t) {
        uint32_t forkCond = getOperandData(0x06, op2, ctxt);
        if(!(forkCond & ctxt->vflag))
            return 0;
    }

    // Allocate memory for the child's stack
    uint8_t *stack = sys_mmap(0x20000, PROT_READ|PROT_WRITE, MAP_ANONYMOUS|MAP_PRIVATE);

    // Get the fork destination
    uint32_t forkDst = getOperandData(0xf0, op1, ctxt);

    // Setup a shared futex word
    uint16_t *ready_flag = (uint16_t *)stack;
    *(ready_flag) = 0;

    // Create the clone arguments
    struct clone_args ca = {0};
    ca.flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND | CLONE_THREAD | CLONE_SYSVSEM;
    ca.stack = (uint64_t)(stack + 0x20000);
    ca.stack_size = 0x20000;

    // Create the child thread
    __asm__ volatile(
        "mov rax, %[c]\n"
        "mov rdi, %[ptr]\n"
        "mov rsi, %[sz]\n"
        "syscall"
        :
        : [c]"i"(SYS_clone3),
            [ptr]"r"(&ca),
            [sz]"i"(sizeof(ca))
        : "rax", "rdi", "rsi"
    );

    // Check the current pid
    long ret;
    __asm__ volatile( "mov %0, rax" : "=r"(ret) : : "rax" );

    // Error in syscall
    if(ret < 0) sys_exit(SYS_clone3);

    // Child thread, update the ready flag and syscall(futex, FUTEX_WAKE)
    if(ret == 0) {
        *(ready_flag) = 1;
        __asm__ volatile(
            "mov rax, %[c]\n"
            "mov rdi, %[f]\n"
            "mov esi, 1\n"
            "mov edx, 1\n"
            "syscall"
            :
            : [c]"i"(SYS_futex),
              [f]"r"(stack)
            : "rax", "rdi", "rsi", "rdx");
        sys_sleep(1, 0);
        emulate(forkDst);
        sys_munmap(stack, 0x20000);
    }

    // Parent thread, wait for the ready flag via syscall(futex, FUTEX_WAIT)
    else {
        do {
            __asm__ volatile(
                "mov rax, %[c]\n"
                "mov rdi, %[f]\n"
                "xor esi, esi\n"
                "xor edx, edx\n"
                "xor r10, r10\n"
                "syscall"
                :
                : [c]"i"(SYS_futex),
                  [f]"r"(stack)
                : "rax", "rdi", "rsi", "rdx", "r10"
            );
        } while(*ready_flag == 0);
    }

    // Return success
    return 0;
}


/*************************************************************/
/* BASIC EMULATION STEPS                                     */
/*************************************************************/

/**
 * Dispatch emulation to an appropriate handler
 * @param vip - Address of the instruction
 * @param opcode - Opcode for the instruction
 * @param op1 - First operand / destination operand
 * @param op2 - Second operand / source operand
 * @return the address of the next instruction
 */
uint32_t dispatch(uint32_t vip, WeeperOpcode opcode, WeeperOperand *op1, WeeperOperand *op2, WeeperContext *ctxt) {
    int jmpDst = 0;

    if(opcode % 6 == 0) {
        if(opcode * 6 == 0)
            handle_mov(op1, op2, ctxt);
        else if(opcode < 10)
            handle_mul(op1, op2, ctxt);
        else if(opcode > 20)
            handle_packlo(op1, op2, ctxt);
        else if(opcode % 9 == 0)
            handle_shr(op1, op2, ctxt);
        else
            jmpDst = handle_ret(op1, op2, ctxt);
    }

    else if(opcode % 3 == 0) {
        if(opcode < 10) {
            if((opcode & 3) == 3)
                handle_get(op1, op2, ctxt);
            else
                handle_cmp(op1, op2, ctxt);
        }
        else if(opcode % 5 == 0)
            handle_or(op1, op2, ctxt);
        else if(opcode % 7 == 0)
            handle_swap(op1, op2, ctxt);
        else
            handle_fork(op1, op2, ctxt);
    }

    else if(opcode % 2 == 0) {
        if(opcode > 10) {
            if(opcode < 20) {
                if(opcode % 7 == 0)
                    handle_and(op1, op2, ctxt);
                else
                    handle_xor(op1, op2, ctxt);
            }
            else if(opcode > 20) {
                if(opcode % 11 == 0)
                    handle_rev(op1, op2, ctxt);
                else
                    handle_ror(op1, op2, ctxt);
            }
            else
                handle_syscall(op1, op2, ctxt);
        }
        else if(opcode < 10) {
            if((opcode & 7) == 0)
                handle_mod(op1, op2, ctxt);
            else if((opcode & 3) == 0)
                handle_add(op1, op2, ctxt);
            else
                handle_put(op1, op2, ctxt);
        }
        else
            jmpDst = handle_jmp(op1, op2, ctxt);
    }
    else {
        if((opcode & 3) == 1) {
            if(opcode > 20)
                handle_rol(op1, op2, ctxt);
            else if(opcode > 10) {
                if(opcode * 3 > 50)
                    handle_shl(op1, op2, ctxt);
                else
                    handle_exit(op1, op2, ctxt);
            }
            else if(opcode % 5 == 0)
                handle_sub(op1, op2, ctxt);
            else
                handle_lea(op1, op2, ctxt);
        }
        else {
            if(opcode > 20)
                handle_packhi(op1, op2, ctxt);
            else if(opcode < 10)
                handle_div(op1, op2, ctxt);
            else if(opcode % 11 == 0)
                jmpDst = handle_call(vip, op1, op2, ctxt);
            else
                handle_not(op1, op2, ctxt);
        }
    }

    // The instruction resulted in a jump
    if(jmpDst)
        return jmpDst - 1;

    // The instruction resulted in a fallthrough
    int skip = vip + (op1->t ? op1->l : 0) + (op2->t ? op2->l : 0) + 3;
    return (vcode[skip] | (vcode[skip+1] << 8) | (vcode[skip+2] << 16)) ^ 0xdc2606;
}


/**
 * Decode the actual opcode and operands
 * @param insnHdr - Instruction header to decode
 * @return the decoded opcode
 */
WeeperOpcode inline __attribute__((always_inline)) decode(uint32_t vip, WeeperInsnHdr insnHdr, WeeperOperand *op1, WeeperOperand *op2) {
    // Create half of operand 1 / destination operand
    op1->l = insnHdr.op1_l + 1;
    op1->data = (void *)(vcode + vip + 3);
    if(insnHdr.xor_t == WEEPER_OPC_XOR_3)
        insnHdr.opcode_l = ~insnHdr.opcode_l;

    // Create half of operand 2 / source operand
    op2->t = insnHdr.op2_t;
    op2->v = insnHdr.op2_v;
    if(insnHdr.xor_t == WEEPER_OPC_XOR_1)
        insnHdr.opcode_r = ~insnHdr.opcode_r;

    // Create remaining half of operand 2 / source operand
    op2->l = insnHdr.op2_l + 1;
    op2->data = (void *)(vcode + vip + 3 + op1->l);
    if(insnHdr.xor_t == WEEPER_OPC_XOR_2)
        insnHdr.opcode_l = ~insnHdr.opcode_l;

    // Create remaining half of operand 1 / destination operand
    op1->t = insnHdr.op1_t;
    op1->v = insnHdr.op1_v;
    if(insnHdr.xor_t == WEEPER_OPC_XOR_3)
        insnHdr.opcode_r = ~insnHdr.opcode_r;

    // Return the decoded opcode
    return (insnHdr.opcode_r ^ insnHdr.opcode_l) & 0x1f;
}


/**
 * Fetch an instruction header
 * @param vip - Address to fetch from
 * @param insnHdr - Destination to parse into
 */
void inline __attribute__((always_inline)) fetch(uint32_t vip, WeeperInsnHdr *insnHdr) {
    uint8_t *rawHdr = (vcode + vip);
    insnHdr->op2_v = ((rawHdr[1] & 1) << 1) | ((rawHdr[2] & 0b10000000) >> 7);
    insnHdr->op2_t = (rawHdr[1] & 0b00000110) >> 1;
    insnHdr->op1_v = ((rawHdr[0] & 1) << 1) | ((rawHdr[1] & 0b10000000) >> 7);
    insnHdr->op2_l = (rawHdr[2] & 0b01100000) >> 5;
    insnHdr->opcode_l = (rawHdr[0] & 0b11111000) >> 3;
    insnHdr->op1_l = (rawHdr[1] & 0b01100000) >> 5;
    insnHdr->op1_t = (rawHdr[0] & 0b00000110) >> 1;
    insnHdr->opcode_r = rawHdr[2] & 0b00011111;
    insnHdr->xor_t = (rawHdr[1] & 0b00011000) >> 3;
}


/**
 * Emulate code after loading from _start
 */
void emulate(uint32_t vip) {
    // Create the virtual context
    uint32_t vregs[16] = {0};
    Stack stk = {0}; stack_init(&stk);
    WeeperContext ctxt = { &vregs[0], &stk, 0 };

    // Perform the emulation loop
    do {
        WeeperInsnHdr insnHdr = {0};
        WeeperOperand op1 = {0};
        WeeperOperand op2 = {0};
        fetch(vip, &insnHdr);
        WeeperOpcode opcode = decode(vip, insnHdr, &op1, &op2);
        vip = dispatch(vip, opcode, &op1, &op2, &ctxt);
    } while(vip != 0xFFFFFF);

    // Cleanup the register data
    return;
 }
