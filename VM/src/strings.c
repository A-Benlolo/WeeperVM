#include <stdint.h>
#include "string.h"
#include "utils.h"


int opaque_predicate_1() {
    unsigned int eax, ebx, ecx, edx;
    __asm__ volatile (
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    return (edx & (1 << 25)) != 0; // SSE supported
}


void proc_self_exe(char *buf, uint32_t key) {
    uint32_t chunk;
    // Chunk 2: "c/se"
    if(!opaque_predicate_1()) {
        chunk = ROR32(0xd386c053, 11) ^ key;
        buf[4] = (char) (((chunk & 0xFF000000) >> 24) & 0xFF);
        buf[5] = (char) (((chunk & 0x00FF0000) >> 16) & 0xFF);
        buf[6] = (char) (((chunk & 0x0000FF00) >> 8) & 0xFF);
        buf[7] = (char) (((chunk & 0x000000FF) >> 0) & 0xFF);
    }
    else {
        chunk = ROR32(0x0eda161f, 4) ^ key;
        buf[4] = (char)((chunk >> 0) & 0xFF);
        buf[5] = (char)((chunk >> 8) & 0xFF);
        buf[6] = (char)((chunk >> 16) & 0xFF);
        buf[7] = (char)((chunk >> 24) & 0xFF);
    }
    // Chunk 4: "xe"
    if(opaque_predicate_1()) {
        chunk = ROR32(0xdd6f52b3, 13) ^ key;
        buf[12] = (char)((chunk >> 0) & 0xFF);
        buf[13] = (char)((chunk >> 8) & 0xFF);
        buf[14] = (char)((chunk >> 16) & 0xFF);
        buf[15] = (char)((chunk >> 24) & 0xFF);
    }
    else {
        chunk = ROR32(0xe6b47181, 12) ^ key;
        buf[12] = (char) (((chunk & 0xFF000000) >> 24) & 0xFF);
        buf[13] = (char) (((chunk & 0x00FF0000) >> 16) & 0xFF);
        buf[14] = (char) (((chunk & 0x0000FF00) >> 8) & 0xFF);
        buf[15] = (char) (((chunk & 0x000000FF) >> 0) & 0xFF);
    }
    // Chunk 3: "lf/e"
    if(opaque_predicate_1()) {
        chunk = ROR32(0x58f43778, 7) ^ key;
        buf[8] = (char)((chunk >> 0) & 0xFF);
        buf[9] = (char)((chunk >> 8) & 0xFF);
        buf[10] = (char)((chunk >> 16) & 0xFF);
        buf[11] = (char)((chunk >> 24) & 0xFF);
    }
    else {
        chunk = ROR32(0x874e73d3, 11) ^ key;
        buf[8] = (char) (((chunk & 0xFF000000) >> 24) & 0xFF);
        buf[9] = (char) (((chunk & 0x00FF0000) >> 16) & 0xFF);
        buf[10] = (char) (((chunk & 0x0000FF00) >> 8) & 0xFF);
        buf[11] = (char) (((chunk & 0x000000FF) >> 0) & 0xFF);
    }
    // Chunk 1: "/pro"
    if(!opaque_predicate_1()) {
        chunk = ROR32(0xd3edb221, 15) ^ key;
        buf[0] = (char) (((chunk & 0xFF000000) >> 24) & 0xFF);
        buf[1] = (char) (((chunk & 0x00FF0000) >> 16) & 0xFF);
        buf[2] = (char) (((chunk & 0x0000FF00) >> 8) & 0xFF);
        buf[3] = (char) (((chunk & 0x000000FF) >> 0) & 0xFF);
    }
    else {
        chunk = ROR32(0xd9fc5bf5, 9) ^ key;
        buf[0] = (char)((chunk >> 0) & 0xFF);
        buf[1] = (char)((chunk >> 8) & 0xFF);
        buf[2] = (char)((chunk >> 16) & 0xFF);
        buf[3] = (char)((chunk >> 24) & 0xFF);
    }
}
