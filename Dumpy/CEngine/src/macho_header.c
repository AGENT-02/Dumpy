#include "macho_header.h"
#include "macho_types.h"
#include "macho_fat.h"
#include "safe_read.h"
#include <string.h>
#include <stdio.h>

const char *macho_file_type_name(uint32_t filetype) {
    static char unknown_buf[32];

    switch (filetype) {
        case MACHO_FILETYPE_OBJECT:     return "Object";
        case MACHO_FILETYPE_EXECUTE:    return "Executable";
        case MACHO_FILETYPE_FVMLIB:     return "Fixed VM Library";
        case MACHO_FILETYPE_CORE:       return "Core";
        case MACHO_FILETYPE_PRELOAD:    return "Preloaded Executable";
        case MACHO_FILETYPE_DYLIB:      return "Dynamic Library";
        case MACHO_FILETYPE_DYLINKER:   return "Dynamic Linker";
        case MACHO_FILETYPE_BUNDLE:     return "Bundle";
        case MACHO_FILETYPE_DYLIB_STUB: return "Dynamic Library Stub";
        case MACHO_FILETYPE_DSYM:       return "Debug Symbols (dSYM)";
        case MACHO_FILETYPE_KEXT:       return "Kext Bundle";
        case MACHO_FILETYPE_FILESET:    return "File Set";
        default:
            snprintf(unknown_buf, sizeof(unknown_buf), "Unknown (type=%u)", filetype);
            return unknown_buf;
    }
}

DiagCode macho_parse_header(const MachOContext *ctx, MachOHeaderInfo *info,
                            DiagList *diags) {
    if (!ctx || !info) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to macho_parse_header");
        return DIAG_ERR_TRUNCATED;
    }

    memset(info, 0, sizeof(MachOHeaderInfo));

    if (ctx->is_64bit) {
        MachOHeader64 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr))) {
            if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                                "Cannot read 64-bit Mach-O header");
            return DIAG_ERR_TRUNCATED;
        }
        info->magic       = macho_swap32(ctx, hdr.magic);
        info->cpu_type    = macho_swap32(ctx, hdr.cputype);
        info->cpu_subtype = macho_swap32(ctx, hdr.cpusubtype);
        info->file_type   = macho_swap32(ctx, hdr.filetype);
        info->ncmds       = macho_swap32(ctx, hdr.ncmds);
        info->sizeofcmds  = macho_swap32(ctx, hdr.sizeofcmds);
        info->flags       = macho_swap32(ctx, hdr.flags);
    } else {
        MachOHeader32 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr))) {
            if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                                "Cannot read 32-bit Mach-O header");
            return DIAG_ERR_TRUNCATED;
        }
        info->magic       = macho_swap32(ctx, hdr.magic);
        info->cpu_type    = macho_swap32(ctx, hdr.cputype);
        info->cpu_subtype = macho_swap32(ctx, hdr.cpusubtype);
        info->file_type   = macho_swap32(ctx, hdr.filetype);
        info->ncmds       = macho_swap32(ctx, hdr.ncmds);
        info->sizeofcmds  = macho_swap32(ctx, hdr.sizeofcmds);
        info->flags       = macho_swap32(ctx, hdr.flags);
    }

    info->is_64bit      = ctx->is_64bit;
    info->arch_name     = cpu_type_name(info->cpu_type, info->cpu_subtype);
    info->file_type_name = macho_file_type_name(info->file_type);

    /* Validate sizeofcmds fits within the file */
    if (!safe_check_range(ctx->size, ctx->header_size, info->sizeofcmds)) {
        if (diags) diag_add_fmt(diags, DIAG_ERR_TRUNCATED, ctx->header_size,
                                "Load commands (size 0x%X) extend beyond file",
                                info->sizeofcmds);
        return DIAG_ERR_TRUNCATED;
    }

    /*
     * Validate that ncmds is consistent with sizeofcmds.
     * Each load command must be at least sizeof(struct load_command)
     * (8 bytes: uint32_t cmd + uint32_t cmdsize), so the minimum
     * space required is ncmds * 8.
     */
    if (info->ncmds > 0) {
        uint64_t min_lc_space = (uint64_t)info->ncmds * 8;
        if (min_lc_space > info->sizeofcmds) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_TRUNCATED, ctx->header_size,
                                    "ncmds (%u) requires at least %llu bytes "
                                    "but sizeofcmds is only %u",
                                    info->ncmds,
                                    (unsigned long long)min_lc_space,
                                    info->sizeofcmds);
            return DIAG_ERR_TRUNCATED;
        }
    }

    return DIAG_OK;
}
