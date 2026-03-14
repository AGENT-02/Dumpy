#include "macho_fat.h"
#include "macho_types.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ARM CPU subtypes */
#define MACHO_CPU_SUBTYPE_ARM_V7   9
#define MACHO_CPU_SUBTYPE_ARM_V7S  11
#define MACHO_CPU_SUBTYPE_ARM_V7K  12

/* x86_64 CPU subtypes */
#define MACHO_CPU_SUBTYPE_X86_64_H 8

/* Fat headers are always big-endian; we need to swap on little-endian hosts. */
static uint32_t fat_swap32(uint32_t val) {
    /* Always swap because fat headers are big-endian and we target LE (arm64/x86_64) */
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x000000FFu) << 24);
}

const char *cpu_type_name(uint32_t cputype, uint32_t cpusubtype) {
    uint32_t subtype = cpusubtype & ~0xFF000000u; /* mask off capability bits */
    static char unknown_buf[64];

    switch (cputype) {
        case MACHO_CPU_TYPE_ARM64:
            switch (subtype) {
                case MACHO_CPU_SUBTYPE_ARM64E:   return "arm64e";
                case MACHO_CPU_SUBTYPE_ARM64_ALL:
                default:                          return "arm64";
            }
        case MACHO_CPU_TYPE_ARM:
            switch (subtype) {
                case MACHO_CPU_SUBTYPE_ARM_V7:   return "armv7";
                case MACHO_CPU_SUBTYPE_ARM_V7S:  return "armv7s";
                case MACHO_CPU_SUBTYPE_ARM_V7K:  return "armv7k";
                default:                          return "arm";
            }
        case MACHO_CPU_TYPE_X86_64:
            if (subtype == MACHO_CPU_SUBTYPE_X86_64_H) return "x86_64h";
            return "x86_64";
        case MACHO_CPU_TYPE_X86:     return "i386";
        default:
            snprintf(unknown_buf, sizeof(unknown_buf),
                     "unknown (cputype=%u, cpusubtype=%u)", cputype, cpusubtype);
            return unknown_buf;
    }
}

DiagCode fat_parse(const uint8_t *data, size_t size, FatInfo *info, DiagList *diags) {
    if (!data || !info) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0, "NULL argument to fat_parse");
        return DIAG_ERR_TRUNCATED;
    }

    memset(info, 0, sizeof(FatInfo));

    /* Read magic */
    uint32_t magic;
    if (!safe_read_uint32(data, size, 0, &magic)) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "File too small to read magic");
        return DIAG_ERR_TRUNCATED;
    }

    bool is_fat_be = (magic == MACHO_FAT_MAGIC);
    bool is_fat_le = (magic == MACHO_FAT_CIGAM);

    if (!is_fat_be && !is_fat_le) {
        /* Not a fat binary; that's fine, not an error */
        info->is_fat = false;
        return DIAG_OK;
    }

    info->is_fat = true;

    /* Read fat header */
    MachOFatHeader fat_hdr;
    if (!safe_read_bytes(data, size, 0, &fat_hdr, sizeof(fat_hdr))) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "File too small for fat header");
        return DIAG_ERR_TRUNCATED;
    }

    /* Fat headers are big-endian. If magic reads as FAT_MAGIC (0xCAFEBABE)
       on this host, the host is big-endian and no swap is needed.
       If magic reads as FAT_CIGAM, the host is little-endian and we swap. */
    bool need_swap = is_fat_le;

    uint32_t narch = need_swap ? fat_swap32(fat_hdr.nfat_arch) : fat_hdr.nfat_arch;

    /* Sanity check: don't allow absurd arch counts */
    if (narch > 256) {
        if (diags) diag_add_fmt(diags, DIAG_ERR_TRUNCATED, 0,
                                "Fat binary claims %u architectures, likely corrupt", narch);
        return DIAG_ERR_TRUNCATED;
    }

    /* Check we have room for all arch entries */
    size_t archs_offset = sizeof(MachOFatHeader);
    size_t archs_size = (size_t)narch * sizeof(MachOFatArch);
    if (!safe_check_range(size, archs_offset, archs_size)) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, archs_offset,
                            "File too small for fat arch entries");
        return DIAG_ERR_TRUNCATED;
    }

    info->narch = narch;
    info->archs = (FatArchInfo *)calloc(narch, sizeof(FatArchInfo));
    if (!info->archs) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate fat arch info array");
        return DIAG_ERR_ALLOC_FAILED;
    }

    for (uint32_t i = 0; i < narch; i++) {
        size_t entry_offset = archs_offset + (size_t)i * sizeof(MachOFatArch);
        MachOFatArch arch;
        if (!safe_read_bytes(data, size, entry_offset, &arch, sizeof(arch))) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_TRUNCATED, entry_offset,
                                    "Cannot read fat arch entry %u", i);
            fat_info_destroy(info);
            return DIAG_ERR_TRUNCATED;
        }

        uint32_t cpu     = need_swap ? fat_swap32(arch.cputype)    : arch.cputype;
        uint32_t subcpu  = need_swap ? fat_swap32(arch.cpusubtype) : arch.cpusubtype;
        uint32_t offset  = need_swap ? fat_swap32(arch.offset)     : arch.offset;
        uint32_t sz      = need_swap ? fat_swap32(arch.size)       : arch.size;
        uint32_t al      = need_swap ? fat_swap32(arch.align)      : arch.align;

        /* Validate that this arch's data is within the file */
        if (!safe_check_range(size, (size_t)offset, (size_t)sz)) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_OFFSET, entry_offset,
                                    "Fat arch %u (offset=0x%X, size=0x%X) extends beyond file",
                                    i, offset, sz);
            fat_info_destroy(info);
            return DIAG_ERR_INVALID_OFFSET;
        }

        /* Validate architecture alignment */
        if (al < 32 && (offset % (1u << al)) != 0) {
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, entry_offset,
                                    "Fat arch %u offset 0x%X not aligned to 2^%u",
                                    i, offset, al);
        }

        info->archs[i].cpu_type    = cpu;
        info->archs[i].cpu_subtype = subcpu;
        info->archs[i].offset      = offset;
        info->archs[i].size         = sz;
        info->archs[i].align        = al;
        info->archs[i].arch_name    = cpu_type_name(cpu, subcpu);
    }

    /* Detect overlapping architecture slices: sort by offset, check adjacency */
    if (narch > 1) {
        /* Simple insertion sort by offset (narch is small, max 256) */
        for (uint32_t i = 1; i < narch; i++) {
            FatArchInfo tmp = info->archs[i];
            uint32_t j = i;
            while (j > 0 && info->archs[j - 1].offset > tmp.offset) {
                info->archs[j] = info->archs[j - 1];
                j--;
            }
            info->archs[j] = tmp;
        }

        for (uint32_t i = 0; i + 1 < narch; i++) {
            uint64_t end = (uint64_t)info->archs[i].offset + info->archs[i].size;
            if (end > info->archs[i + 1].offset) {
                if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, 0,
                                        "Fat arch slices overlap: arch at offset 0x%X+0x%X overlaps arch at 0x%X",
                                        info->archs[i].offset, info->archs[i].size,
                                        info->archs[i + 1].offset);
            }
        }
    }

    return DIAG_OK;
}

void fat_info_destroy(FatInfo *info) {
    if (!info) return;
    free(info->archs);
    info->archs = NULL;
    info->narch = 0;
    info->is_fat = false;
}
