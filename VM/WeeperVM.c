#include "emulate.h"
#include "syscall.h"
#include "strings.h"
#include "utils.h"

/**
 * Virtual code
 * Stored at end of ELF file
 * (Compiled and saved using WeeperVM_compile.py)
 */
uint8_t *vcode;

/**
 * 1MB of Virtual memory
 * 0x000000 - 0x00FFFF reserved for local variables
 * 0x010000 - 0xFFFFFF volatile memory... anything goes
 */
uint8_t *vmem;


/**
 * Load virtual code from the end of this ELF file.
 */
uint32_t load_vcode() {
    // Open /proc/self/exe
    char procSelfExe[16];
    proc_self_exe(&(procSelfExe[0]), 0x959e8e02);
    int self_fd = sys_open(procSelfExe, O_RDONLY);
    if(self_fd < 0) {
        sys_exit(1);
    }

    // Get the total size of the file
    int self_size = sys_lseek(self_fd, 0, SEEK_END);

    // Read the encrypted length of the file
    sys_lseek(self_fd, self_size - 8, SEEK_SET);
    uint8_t vcode_lenAsBytes[8];
    uint8_t *vcode_lenHiAsBytes = &vcode_lenAsBytes[0];
    uint8_t *vcode_lenLoAsBytes = &vcode_lenAsBytes[0] + 4;
    sys_read(self_fd, &(vcode_lenAsBytes[0]), 8);

    // Read the ELF header (used in decryption)
    sys_lseek(self_fd, 0, SEEK_SET);
    uint8_t elfHdr[4];
    sys_read(self_fd, &(elfHdr[0]), 4);

    // Parse the encrypted length into two big-endian ints
    uint32_t vcode_lenLo = 0;
    uint32_t vcode_lenHi = 0;
    for(int i = 0; i < 4; i+=2) {
        uint16_t lo = vcode_lenLoAsBytes[i+1] | (vcode_lenLoAsBytes[i] << 8);
        vcode_lenLo |= lo << (16 - 8 * i);
        uint16_t hi = vcode_lenHiAsBytes[i+1] | (vcode_lenHiAsBytes[i] << 8);
        vcode_lenHi |= hi << (16 - 8 * i);
    }

    // Parse the ELF header into a little-endian int
    uint32_t kElf = 0;
    for(int i = 0; i < 4; i+=2) {
        uint16_t word = elfHdr[i] | (elfHdr[i+1] << 8);
        kElf |= word << (8 * i);
    }

    // Perform the decryption process
    uint32_t kHi = ROL32(vcode_lenHi, 13);
    uint32_t kLo = ROR32(vcode_lenLo, 27);
    uint32_t kMix = kHi ^ kLo;
    uint32_t vcode_len = kElf ^ kMix;

    // Read the virtual code into memory
    vcode = (uint8_t *)sys_mmap(vcode_len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS);
    sys_lseek(self_fd, self_size - 8 - vcode_len, SEEK_SET);
    sys_read(self_fd, vcode, vcode_len);

    // Mark code as read only
    sys_mprotect(vcode, vcode_len, PROT_READ);

    // Close the file
    sys_close(self_fd);

    // Return the length of the vcode
    return vcode_len;
}


void _start(void) {
    // Define the known memory lengths
    uint32_t vmem_len = WEEPER_VMEM_SIZE;

    // Setup the virtual memory and code (virtual registers are thread-specific)
    vmem = (uint8_t *)sys_mmap(vmem_len, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS);
    uint32_t vcode_len = load_vcode();

    // Perform the fetch-decode-dispatch-handle loop
    emulate(0);

    // Cleanup and return
    sys_munmap(vmem, vmem_len);
    sys_munmap(vcode, vcode_len);
    sys_exit_group(-1);
}
