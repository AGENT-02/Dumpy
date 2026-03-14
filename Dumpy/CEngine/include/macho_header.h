#ifndef MACHO_HEADER_H
#define MACHO_HEADER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "macho_reader.h"
#include "diagnostics.h"

typedef struct {
    uint32_t magic;
    uint32_t cpu_type;
    uint32_t cpu_subtype;
    uint32_t file_type;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    bool is_64bit;
    const char *arch_name;
    const char *file_type_name;
} MachOHeaderInfo;

/// Parse the Mach-O header from an initialized context.
DiagCode macho_parse_header(const MachOContext *ctx, MachOHeaderInfo *info,
                            DiagList *diags);

/// Return a human-readable string for a Mach-O file type constant.
const char *macho_file_type_name(uint32_t filetype);

#endif /* MACHO_HEADER_H */
