#define ROR32(x, r) (((x) >> (r)) | ((x) << (32 - (r))))
#define ROL32(x, r) (((x) << (r)) | ((x) >> (32 - (r))))

#define REVERSE_ENDIAN_INPLACE(ptr, n) do {         \
    uint8_t* p = (uint8_t*)(ptr);                   \
    for (int i = 0; i < (n) / 2; ++i) {             \
        uint8_t tmp = p[i];                         \
        p[i] = p[(n) - 1 - i];                      \
        p[(n) - 1 - i] = tmp;                       \
    }                                               \
} while (0)
