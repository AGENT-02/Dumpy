#ifndef MACHO_READER_H
#define MACHO_READER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "diagnostics.h"

typedef struct {
    const uint8_t *data;
    size_t size;
    bool is_64bit;
    bool needs_swap; /* true if binary endianness differs from host */
    uint32_t magic;
    size_t header_size; /* sizeof MachOHeader32 or MachOHeader64 */
} MachOContext;

/// Initialize context from raw file data, validating the header.
DiagCode macho_context_init(MachOContext *ctx, const uint8_t *data, size_t size,
                            DiagList *diags);

/// Byte-swap helper: swaps only if ctx->needs_swap is true.
uint32_t macho_swap32(const MachOContext *ctx, uint32_t val);

/// Byte-swap helper: swaps only if ctx->needs_swap is true.
uint64_t macho_swap64(const MachOContext *ctx, uint64_t val);

#endif /* MACHO_READER_H */
