#ifndef MACHO_FAT_H
#define MACHO_FAT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "diagnostics.h"

typedef struct {
    uint32_t cpu_type;
    uint32_t cpu_subtype;
    uint32_t offset;
    uint32_t size;
    uint32_t align;
    const char *arch_name; /* human-readable, e.g. "arm64", "arm64e", "x86_64" */
} FatArchInfo;

typedef struct {
    bool is_fat;
    uint32_t narch;
    FatArchInfo *archs; /* array of narch entries */
} FatInfo;

/// Parse a potential fat/universal binary. If not fat, sets info->is_fat = false.
DiagCode fat_parse(const uint8_t *data, size_t size, FatInfo *info, DiagList *diags);

/// Free resources associated with a FatInfo.
void fat_info_destroy(FatInfo *info);

/// Return a human-readable name for the given CPU type/subtype combination.
const char *cpu_type_name(uint32_t cputype, uint32_t cpusubtype);

#endif /* MACHO_FAT_H */
