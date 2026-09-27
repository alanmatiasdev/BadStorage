// License: https://github.com/EatonZ/BadStorage/blob/main/LICENSE
// Retail 17559 only. Runs as a title before Aurora; no XDK or network at boot.

#include <stdint.h>
#include <stddef.h>

// Clang may lower aggregate initialization to these even in freestanding mode.
__attribute__((optnone)) void *memcpy(void *destination, const void *source, size_t size)
{
    uint8_t *to = destination;
    const uint8_t *from = source;
    for (size_t i = 0; i < size; ++i) to[i] = from[i];
    return destination;
}

__attribute__((optnone)) void *memset(void *destination, int value, size_t size)
{
    uint8_t *to = destination;
    for (size_t i = 0; i < size; ++i) to[i] = (uint8_t)value;
    return destination;
}

#define SDINIT_ADDRESS       0x8015DB18u
#define SDINIT_LENGTH        0x5FCu
#define SATA_SECTORS_ADDRESS 0x801A61ECu
#define POWER_LIST_HEAD     0x801704C8u
#define SATA_POWER_RECORD   0x80170B90u
#define XAM_ADD_REMOVE      0x8167E918u
#define XAM_BROADCAST       0x816E2DF8u
#define CONTENT_OFFSET      UINT64_C(0x130EB0000)
#define HDD_FLAG            0x20u
#define DEVICE_INITIALIZING 0x10u
#define MAX_DEVICES         16u
#define MAX_REFERENCES      4u

// The only linked kernel imports. All version-specific exports are checked
// against the console's known 17559 addresses before any kernel write.
extern uint32_t XexGetModuleHandle(const char *, void **);
extern uint32_t XexGetProcedureAddress(void *, uint32_t, void **);

typedef struct {
    uint16_t length;
    uint16_t maximum_length;
    char *buffer;
} object_string_t;

typedef struct {
    const char *name;
    uint64_t offset;
    uint64_t length;
    uint8_t overlaps_dump;
} fixed_partition_t;

typedef struct {
    char name[32];
    uint8_t *object;
    uint8_t *extension;
    uint32_t flags;
    uint64_t offset;
    uint64_t length;
    uint8_t write_geometry;
} disk_device_t;

typedef struct {
    char *path;
    uint32_t action;
    uint32_t type;
    uint32_t device_id;
} device_task_t;

#ifndef BOOT_HOST_TEST
_Static_assert(sizeof(void *) == 4, "Xbox 360 pointers must be 32 bits");
_Static_assert(sizeof(object_string_t) == 8, "OBJECT_STRING layout changed");
_Static_assert(sizeof(device_task_t) == 16, "XContent task layout changed");
_Static_assert(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__, "Xbox 360 is big endian");
#endif

typedef uint32_t (*reference_t)(object_string_t *, uint32_t, void *, void *, void **);
typedef void (*dereference_t)(void *);
typedef uint32_t (*dump_info_t)(void *, uint32_t);
typedef void (*register_power_t)(void *, uint8_t);
typedef uint8_t (*add_remove_t)(device_task_t *);
typedef void (*broadcast_t)(void);
typedef void (*launch_t)(const char *, uint32_t);
typedef void (*terminate_t)(void);
typedef void *(*create_file_t)(const char *, uint32_t, uint32_t, void *, uint32_t, uint32_t, void *);
typedef int (*write_file_t)(void *, const void *, uint32_t, uint32_t *, void *);
typedef int (*close_handle_t)(void *);

static const uint8_t sdinit_digest[32] = {
    0x3B, 0x52, 0xFF, 0x82, 0x27, 0x34, 0xEB, 0x7D,
    0x4B, 0x17, 0x4D, 0x6F, 0x7F, 0x76, 0x09, 0x6C,
    0xC3, 0x39, 0x4D, 0x81, 0x62, 0x9C, 0xB9, 0x65,
    0x0A, 0x2E, 0xB0, 0xC3, 0x5C, 0x8B, 0x3D, 0xB4
};

static const uint32_t sha_k[64] = {
    0x428A2F98, 0x71374491, 0xB5C0FBCF, 0xE9B5DBA5,
    0x3956C25B, 0x59F111F1, 0x923F82A4, 0xAB1C5ED5,
    0xD807AA98, 0x12835B01, 0x243185BE, 0x550C7DC3,
    0x72BE5D74, 0x80DEB1FE, 0x9BDC06A7, 0xC19BF174,
    0xE49B69C1, 0xEFBE4786, 0x0FC19DC6, 0x240CA1CC,
    0x2DE92C6F, 0x4A7484AA, 0x5CB0A9DC, 0x76F988DA,
    0x983E5152, 0xA831C66D, 0xB00327C8, 0xBF597FC7,
    0xC6E00BF3, 0xD5A79147, 0x06CA6351, 0x14292967,
    0x27B70A85, 0x2E1B2138, 0x4D2C6DFC, 0x53380D13,
    0x650A7354, 0x766A0ABB, 0x81C2C92E, 0x92722C85,
    0xA2BFE8A1, 0xA81A664B, 0xC24B8B70, 0xC76C51A3,
    0xD192E819, 0xD6990624, 0xF40E3585, 0x106AA070,
    0x19A4C116, 0x1E376C08, 0x2748774C, 0x34B0BCB5,
    0x391C0CB3, 0x4ED8AA4A, 0x5B9CCA4F, 0x682E6FF3,
    0x748F82EE, 0x78A5636F, 0x84C87814, 0x8CC70208,
    0x90BEFFFA, 0xA4506CEB, 0xBEF9A3F7, 0xC67178F2
};

static const fixed_partition_t fixed_partitions[] = {
    {"Cache0",                  UINT64_C(0x80000),     UINT64_C(0x80000000), 0},
    {"Cache1",                  UINT64_C(0x80080000),  UINT64_C(0x80000000), 0},
    {"DumpPartition",           UINT64_C(0x100080000), UINT64_C(0x20E30000), 0},
    {"SystemURLCachePartition", UINT64_C(0x100080000), UINT64_C(0x6000000),  1},
    {"TitleURLCachePartition",  UINT64_C(0x106080000), UINT64_C(0x2000000),  1},
    {"SystemExtPartition",      UINT64_C(0x10C080000), UINT64_C(0xCE30000),  1},
    {"SystemAuxPartition",      UINT64_C(0x118EB0000), UINT64_C(0x8000000),  1},
    {"SystemPartition",         UINT64_C(0x120EB0000), UINT64_C(0x10000000), 0},
    {"WindowsPartition",        0,                    0,                    0}
};

static uint32_t word_at(const void *base, uint32_t offset)
{
    return *(const volatile uint32_t *)((const uint8_t *)base + offset);
}

static int same_bytes(const void *a, const void *b, size_t length)
{
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < length; ++i) if (x[i] != y[i]) return 0;
    return 1;
}

static int same_string(const char *a, const char *b)
{
    while (*a && *b && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static int valid_system_pointer(uint32_t pointer)
{
    // The live console places Harddisk0 objects around 0x3A000000, whereas
    // static kernel objects and power records are around 0x80000000.
    return (((pointer >= 0x30000000u && pointer < 0x40000000u) ||
             (pointer >= 0x80000000u && pointer < 0xA0000000u)) &&
            (pointer & 3u) == 0);
}

static uint32_t rotate_right(uint32_t x, unsigned n)
{
    return (x >> n) | (x << (32 - n));
}

static void sha_block(uint32_t state[8], const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i)
        w[i] = ((uint32_t)block[i * 4] << 24) | ((uint32_t)block[i * 4 + 1] << 16)
             | ((uint32_t)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(w[i - 15], 7) ^ rotate_right(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotate_right(w[i - 2], 17) ^ rotate_right(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t t1 = h + (rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25))
                    + ((e & f) ^ (~e & g)) + sha_k[i] + w[i];
        uint32_t t2 = (rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22))
                    + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256(const uint8_t *data, uint32_t length, uint8_t digest[32])
{
    uint32_t state[8] = {
        0x6A09E667, 0xBB67AE85, 0x3C6EF372, 0xA54FF53A,
        0x510E527F, 0x9B05688C, 0x1F83D9AB, 0x5BE0CD19
    };
    uint32_t full = length & ~63u;
    for (uint32_t i = 0; i < full; i += 64) sha_block(state, data + i);
    uint8_t tail[128] = {0};
    uint32_t rest = length - full;
    uint32_t padded = rest < 56 ? 64 : 128;
    for (uint32_t i = 0; i < rest; ++i) tail[i] = data[full + i];
    tail[rest] = 0x80;
    uint64_t bit_count = (uint64_t)length << 3;
    for (unsigned i = 0; i < 8; ++i) tail[padded - 1 - i] = (uint8_t)(bit_count >> (i * 8));
    for (uint32_t i = 0; i < padded; i += 64) sha_block(state, tail + i);
    for (unsigned i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)state[i];
    }
}

static int write_checked(void *destination, const void *source, uint32_t size)
{
    volatile uint8_t *to = destination;
    const uint8_t *from = source;
    for (uint32_t i = 0; i < size; ++i) to[i] = from[i];
    __sync_synchronize();
    for (uint32_t i = 0; i < size; ++i) if (to[i] != from[i]) return 0;
    return 1;
}

static int geometry_for(const char *name, uint64_t disk_size,
                        uint64_t *offset, uint64_t *length, uint8_t *overlaps_dump)
{
    *overlaps_dump = 0;
    if (same_string(name, "PhysicalDisk") || same_string(name, "Partition0")) {
        *offset = 0; *length = disk_size; return 1;
    }
    if (same_string(name, "Partition1")) {
        *offset = CONTENT_OFFSET; *length = disk_size - CONTENT_OFFSET; return 1;
    }
    for (unsigned i = 0; i < sizeof(fixed_partitions) / sizeof(fixed_partitions[0]); ++i) {
        if (same_string(name, fixed_partitions[i].name)) {
            *offset = fixed_partitions[i].offset;
            *length = fixed_partitions[i].length;
            *overlaps_dump = fixed_partitions[i].overlaps_dump;
            return 1;
        }
    }
    return 0;
}

static int power_registered(void)
{
    uint32_t node = word_at((void *)POWER_LIST_HEAD, 0);
    for (unsigned count = 0; count < 64 && node != POWER_LIST_HEAD; ++count) {
        if (!valid_system_pointer(node)) return -1;
        if (node >= SATA_POWER_RECORD && node < SATA_POWER_RECORD + 0x10) return 1;
        node = word_at((void *)(uintptr_t)node, 0);
    }
    return node == POWER_LIST_HEAD ? 0 : -1;
}

static int reference_named(reference_t reference, void *type, char *path,
                           void **references, unsigned *reference_count, void **out)
{
    size_t length = 0;
    while (path[length]) ++length;
    object_string_t name = { (uint16_t)length, (uint16_t)(length + 1), path };
    *out = 0;
    if (*reference_count == MAX_REFERENCES ||
        (int32_t)reference(&name, 0, type, 0, out) < 0 || !*out) return 0;
    references[(*reference_count)++] = *out;
    return 1;
}

static int resolve_exact(void *module, uint32_t ordinal, uint32_t expected, void **out)
{
    *out = 0;
    return (int32_t)XexGetProcedureAddress(module, ordinal, out) >= 0 &&
           (uint32_t)(uintptr_t)*out == expected;
}

// Returns a stable failure code; every failure is before the first write except
// codes 14-17, where the changed state remains recoverable on the next boot.
static int activate(void)
{
    void *kernel = 0, *directory = 0, *physical = 0;
    void *objects[MAX_REFERENCES] = {0};
    unsigned reference_count = 0;
    void *directory_type = 0, *device_type = 0, *hardware = 0, *version = 0;
    void *reference_proc = 0, *dereference_proc = 0, *dump_proc = 0, *power_proc = 0;
    disk_device_t devices[MAX_DEVICES];
    unsigned device_count = 0;
    int result = 0;

    if ((int32_t)XexGetModuleHandle("xboxkrnl.exe", &kernel) < 0 || !kernel) return 1;
    if (!resolve_exact(kernel, 344, 0x8004044C, &version) ||
        !resolve_exact(kernel, 342, 0x80170000, &hardware) ||
        !resolve_exact(kernel, 262, 0x80042660, &directory_type) ||
        !resolve_exact(kernel, 58, 0x80040B08, &device_type) ||
        !resolve_exact(kernel, 273, 0x8008B908, &reference_proc) ||
        !resolve_exact(kernel, 261, 0x8008B928, &dereference_proc) ||
        !resolve_exact(kernel, 5, 0x800B90C8, &dump_proc) ||
        !resolve_exact(kernel, 38, 0x80067A58, &power_proc)) return 2;
    if (*(const volatile uint16_t *)((const uint8_t *)version + 4) != 17559) return 3;
    uint8_t digest[32];
    sha256((const uint8_t *)SDINIT_ADDRESS, SDINIT_LENGTH, digest);
    if (!same_bytes(digest, sdinit_digest, sizeof(digest))) return 4;
    if (word_at((void *)XAM_ADD_REMOVE, 0) != 0x7D8802A6u ||
        word_at((void *)XAM_BROADCAST, 0) != 0x7D8802A6u) return 5;
    uint64_t disk_size = (uint64_t)word_at((void *)SATA_SECTORS_ADDRESS, 0) << 9;
    if (disk_size <= CONTENT_OFFSET) return 6;

    reference_t reference = (reference_t)reference_proc;
    dereference_t dereference = (dereference_t)dereference_proc;
    char dir_path[] = "\\Device\\Harddisk0";
    char physical_path[] = "\\Device\\Harddisk0\\PhysicalDisk";
    char p0_path[] = "\\Device\\Harddisk0\\Partition0";
    char p1_path[] = "\\Device\\Harddisk0\\Partition1";
    if (!reference_named(reference, directory_type, dir_path, objects, &reference_count, &directory) ||
        !reference_named(reference, device_type, physical_path, objects, &reference_count, &physical)) {
        result = 7; goto cleanup;
    }
    int found_physical = 0;
    for (unsigned bucket = 0; bucket < 13; ++bucket) {
        uint32_t entry = word_at(directory, bucket * 4);
        unsigned depth = 0;
        while (entry) {
            if (++depth > 32 || device_count == MAX_DEVICES || !valid_system_pointer(entry)) {
                result = 8; goto cleanup;
            }
            uint8_t *location = (uint8_t *)(uintptr_t)entry;
            uint16_t length = *(const volatile uint16_t *)(location + 8);
            uint32_t name_pointer = word_at(location, 12);
            if (!length || length >= sizeof(devices[0].name) || !valid_system_pointer(name_pointer)) {
                result = 8; goto cleanup;
            }
            disk_device_t *item = &devices[device_count++];
            for (unsigned i = 0; i < length; ++i)
                item->name[i] = *(const volatile char *)(uintptr_t)(name_pointer + i);
            item->name[length] = 0;
            item->object = location + 0x20;
            if (same_string(item->name, "PhysicalDisk")) found_physical = item->object == physical;
            entry = word_at(location, 0);
        }
    }
    if (!found_physical || !device_count) { result = 9; goto cleanup; }
    int power = power_registered();
    if (power < 0) { result = 10; goto cleanup; }
    uint8_t raw_dump_buffer[0x100] = {0};
    uint32_t raw_dump = ((dump_info_t)dump_proc)(raw_dump_buffer, 0);
    uint32_t driver = word_at(physical, 8);
    unsigned pending = 0;
    for (unsigned i = 0; i < device_count; ++i) {
        disk_device_t *item = &devices[i];
        item->flags = word_at(item->object, 0x14);
        item->extension = (uint8_t *)(uintptr_t)word_at(item->object, 0x18);
        item->write_geometry = 0;
        if (!(item->flags & DEVICE_INITIALIZING) || word_at(item->object, 8) != driver) continue;
        uint8_t overlaps = 0;
        if (!valid_system_pointer((uint32_t)(uintptr_t)item->extension) ||
            !geometry_for(item->name, disk_size, &item->offset, &item->length, &overlaps)) {
            result = 11; goto cleanup;
        }
        if (raw_dump && overlaps) continue;
        item->write_geometry = 1;
        ++pending;
    }

    // Match SataDiskInitialize's order: geometry first, then clear INITIALIZING.
    for (unsigned i = 0; i < device_count; ++i) {
        disk_device_t *item = &devices[i];
        if (!item->write_geometry) continue;
        uint32_t flags = item->flags & ~DEVICE_INITIALIZING;
        if (word_at(item->object, 0x14) != item->flags ||
            !write_checked(item->extension, &item->offset, 8) ||
            !write_checked(item->extension + 8, &item->length, 8) ||
            !write_checked(item->object + 0x14, &flags, 4)) {
            result = 14; goto cleanup;
        }
    }
    uint32_t original_flags = word_at(hardware, 0);
    uint32_t updated_flags = original_flags | HDD_FLAG;
    if (original_flags != updated_flags && !write_checked(hardware, &updated_flags, 4)) {
        result = 15; goto cleanup;
    }
    if (!power) {
        ((register_power_t)power_proc)((void *)SATA_POWER_RECORD, 1);
        if (power_registered() != 1) { result = 16; goto cleanup; }
    }
    void *p0 = 0, *p1 = 0;
    if (!reference_named(reference, device_type, p0_path, objects, &reference_count, &p0) ||
        !reference_named(reference, device_type, p1_path, objects, &reference_count, &p1)) {
        result = 17; goto cleanup;
    }
    if (pending || !(original_flags & HDD_FLAG)) {
        device_task_t task = {p1_path, 1, 1, 0};
        if (!((add_remove_t)XAM_ADD_REMOVE)(&task)) { result = 18; goto cleanup; }
        ((broadcast_t)XAM_BROADCAST)();
    }

cleanup:
    for (unsigned i = 0; i < reference_count; ++i) dereference(objects[i]);
    return result;
}

static void write_result_log(void *xam, int status)
{
    // Optional on-device trace: no network or JRPC needed to learn whether
    // this title actually ran. The same XAM file APIs are used by XeUnshackle.
    void *create_proc = 0, *write_proc = 0, *close_proc = 0;
    if ((int32_t)XexGetProcedureAddress(xam, 1095, &create_proc) < 0 || !create_proc ||
        (int32_t)XexGetProcedureAddress(xam, 1054, &write_proc) < 0 || !write_proc ||
        (int32_t)XexGetProcedureAddress(xam, 1044, &close_proc) < 0 || !close_proc) return;
    void *file = ((create_file_t)create_proc)("GAME:\\BadStorageBoot.log",
        0x40000000u, 0, 0, 2, 0x80, 0);
    if (!file || (uintptr_t)file == UINT32_MAX) return;
    char line[] = "BadStorage boot: E00\r\n";
    if (!status) { line[17] = 'O'; line[18] = 'K'; line[19] = ' '; }
    else { line[18] = (char)('0' + status / 10); line[19] = (char)('0' + status % 10); }
    uint32_t written = 0;
    ((write_file_t)write_proc)(file, line, (uint32_t)(sizeof(line) - 1), &written, 0);
    ((close_handle_t)close_proc)(file);
}

int main(void)
{
    // GAME: refers to this XEX's directory, including Aurora.xex and the log.
    void *xam = 0, *launch_proc = 0, *terminate_proc = 0;
    if ((int32_t)XexGetModuleHandle("xam.xex", &xam) < 0 || !xam ||
        (int32_t)XexGetProcedureAddress(xam, 420, &launch_proc) < 0 || !launch_proc ||
        (int32_t)XexGetProcedureAddress(xam, 425, &terminate_proc) < 0 || !terminate_proc)
        return 20;
    write_result_log(xam, 99); // E99 means startup reached, but activation did not finish.
    int result = activate();
    write_result_log(xam, result);
    ((launch_t)launch_proc)("GAME:\\Aurora.xex", 0);
    // Terminate this title explicitly after queuing the switch. Returning from
    // a freestanding entry point crashes on some DashLaunch/XAM combinations;
    // leaving the title resident interferes with later game-to-Aurora exits.
    ((terminate_t)terminate_proc)();
    for (;;) { }
}
