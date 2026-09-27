// Host-only checks for the algorithm shared with the Xbox title.
#define BOOT_HOST_TEST 1
#define main boot_title_main
#include "boot.c"
#undef main

uint32_t XexGetModuleHandle(const char *name, void **module)
{
    (void)name; (void)module; return 1;
}

uint32_t XexGetProcedureAddress(void *module, uint32_t ordinal, void **procedure)
{
    (void)module; (void)ordinal; (void)procedure; return 1;
}

int main(void)
{
    static const uint8_t empty_hash[32] = {
        0xE3,0xB0,0xC4,0x42,0x98,0xFC,0x1C,0x14,
        0x9A,0xFB,0xF4,0xC8,0x99,0x6F,0xB9,0x24,
        0x27,0xAE,0x41,0xE4,0x64,0x9B,0x93,0x4C,
        0xA4,0x95,0x99,0x1B,0x78,0x52,0xB8,0x55
    };
    static const uint8_t abc_hash[32] = {
        0xBA,0x78,0x16,0xBF,0x8F,0x01,0xCF,0xEA,
        0x41,0x41,0x40,0xDE,0x5D,0xAE,0x22,0x23,
        0xB0,0x03,0x61,0xA3,0x96,0x17,0x7A,0x9C,
        0xB4,0x10,0xFF,0x61,0xF2,0x00,0x15,0xAD
    };
    uint8_t digest[32];
    sha256((const uint8_t *)"", 0, digest);
    if (!same_bytes(digest, empty_hash, 32)) return 1;
    sha256((const uint8_t *)"abc", 3, digest);
    if (!same_bytes(digest, abc_hash, 32)) return 2;
    uint64_t offset, length;
    uint8_t overlaps;
    uint64_t disk = UINT64_C(250000000000);
    if (!geometry_for("Partition1", disk, &offset, &length, &overlaps) ||
        offset != CONTENT_OFFSET || length != disk - CONTENT_OFFSET || overlaps) return 3;
    if (!geometry_for("SystemAuxPartition", disk, &offset, &length, &overlaps) ||
        offset != UINT64_C(0x118EB0000) || length != UINT64_C(0x8000000) || !overlaps) return 4;
    if (geometry_for("Unknown", disk, &offset, &length, &overlaps)) return 5;
    return 0;
}
