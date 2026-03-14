#include "macho_reader.h"
#include "macho_types.h"
#include "safe_read.h"
#include <string.h>

static uint32_t swap32(uint32_t val) {
    return ((val & 0xFF000000u) >> 24) |
           ((val & 0x00FF0000u) >> 8)  |
           ((val & 0x0000FF00u) << 8)  |
           ((val & 0x000000FFu) << 24);
}

static uint64_t swap64(uint64_t val) {
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >> 8)  |
           ((val & 0x00000000FF000000ULL) << 8)  |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
}

DiagCode macho_context_init(MachOContext *ctx, const uint8_t *data, size_t size,
                            DiagList *diags) {
    if (!ctx || !data) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0, "NULL data pointer");
        return DIAG_ERR_TRUNCATED;
    }

    memset(ctx, 0, sizeof(MachOContext));
    ctx->data = data;
    ctx->size = size;

    /* Need at least 4 bytes for magic */
    uint32_t magic;
    if (!safe_read_uint32(data, size, 0, &magic)) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "File too small to contain Mach-O magic");
        return DIAG_ERR_TRUNCATED;
    }

    ctx->magic = magic;

    switch (magic) {
        case MACHO_MAGIC_64:
            ctx->is_64bit = true;
            ctx->needs_swap = false;
            ctx->header_size = sizeof(MachOHeader64);
            break;
        case MACHO_CIGAM_64:
            ctx->is_64bit = true;
            ctx->needs_swap = true;
            ctx->header_size = sizeof(MachOHeader64);
            break;
        case MACHO_MAGIC_32:
            ctx->is_64bit = false;
            ctx->needs_swap = false;
            ctx->header_size = sizeof(MachOHeader32);
            break;
        case MACHO_CIGAM_32:
            ctx->is_64bit = false;
            ctx->needs_swap = true;
            ctx->header_size = sizeof(MachOHeader32);
            break;
        default:
            if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_MAGIC, 0,
                                    "Invalid Mach-O magic: 0x%08X", magic);
            return DIAG_ERR_INVALID_MAGIC;
    }

    /* Verify we have enough data for the full header */
    if (!safe_check_range(size, 0, ctx->header_size)) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "File too small for Mach-O header");
        return DIAG_ERR_TRUNCATED;
    }

    return DIAG_OK;
}

uint32_t macho_swap32(const MachOContext *ctx, uint32_t val) {
    return ctx->needs_swap ? swap32(val) : val;
}

uint64_t macho_swap64(const MachOContext *ctx, uint64_t val) {
    return ctx->needs_swap ? swap64(val) : val;
}
