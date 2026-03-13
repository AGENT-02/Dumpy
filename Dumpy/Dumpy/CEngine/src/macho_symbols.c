#include "macho_symbols.h"
#include "macho_types.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>

#define MACHO_MAX_SYMBOLS 50000
#define MACHO_MAX_SYMBOL_NAME 4096

static SymbolType classify_ntype(uint8_t n_type) {
    if (n_type & MACHO_N_STAB)
        return SYM_DEBUG;

    switch (n_type & MACHO_N_TYPE) {
        case MACHO_N_UNDF: return SYM_UNDEFINED;
        case MACHO_N_ABS:  return SYM_ABSOLUTE;
        case MACHO_N_SECT: return SYM_SECTION;
        case MACHO_N_PBUD: return SYM_PREBOUND;
        case MACHO_N_INDR: return SYM_INDIRECT;
        default:           return SYM_UNDEFINED;
    }
}

static SymbolVisibility classify_visibility(uint8_t n_type) {
    if (n_type & MACHO_N_PEXT)
        return SYM_VIS_PRIVATE_EXTERNAL;
    if (n_type & MACHO_N_EXT)
        return SYM_VIS_EXTERNAL;
    return SYM_VIS_LOCAL;
}

DiagCode macho_parse_symbols(
    const MachOContext *ctx,
    uint32_t symoff, uint32_t nsyms,
    uint32_t stroff, uint32_t strsize,
    SymbolTable *result, DiagList *diags)
{
    if (!ctx || !result) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to macho_parse_symbols");
        return DIAG_ERR_TRUNCATED;
    }

    memset(result, 0, sizeof(SymbolTable));

    if (nsyms == 0)
        return DIAG_OK;

    /* Validate string table bounds */
    if (!safe_check_range(ctx->size, (size_t)stroff, (size_t)strsize)) {
        if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_OFFSET, stroff,
                                "String table (offset 0x%X, size 0x%X) exceeds file bounds",
                                stroff, strsize);
        return DIAG_ERR_INVALID_OFFSET;
    }

    /* Validate symbol table bounds */
    size_t nlist_size = ctx->is_64bit ? sizeof(MachONlist64) : sizeof(MachONlist32);
    size_t sym_table_size = (size_t)nsyms * nlist_size;

    if (!safe_check_range(ctx->size, (size_t)symoff, sym_table_size)) {
        if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_OFFSET, symoff,
                                "Symbol table (offset 0x%X, %u entries) exceeds file bounds",
                                symoff, nsyms);
        return DIAG_ERR_INVALID_OFFSET;
    }

    /* Cap symbol count */
    uint32_t actual_nsyms = nsyms;
    if (actual_nsyms > MACHO_MAX_SYMBOLS) {
        if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, symoff,
                                "Symbol count %u exceeds maximum %d, capping",
                                nsyms, MACHO_MAX_SYMBOLS);
        actual_nsyms = MACHO_MAX_SYMBOLS;
    }

    /* Allocate symbol entries */
    result->symbols = (SymbolEntry *)calloc(actual_nsyms, sizeof(SymbolEntry));
    if (!result->symbols) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate symbol table");
        return DIAG_ERR_ALLOC_FAILED;
    }

    size_t local_count = 0;
    size_t external_count = 0;
    size_t undefined_count = 0;

    for (uint32_t i = 0; i < actual_nsyms; i++) {
        size_t entry_off = (size_t)symoff + (size_t)i * nlist_size;

        uint32_t n_strx;
        uint8_t  n_type;
        uint8_t  n_sect;
        uint64_t n_value;

        if (ctx->is_64bit) {
            MachONlist64 nlist;
            if (!safe_read_bytes(ctx->data, ctx->size, entry_off, &nlist, sizeof(nlist))) {
                if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_OFFSET, entry_off,
                                        "Cannot read nlist64 entry %u at 0x%zX", i, entry_off);
                break;
            }
            n_strx  = macho_swap32(ctx, nlist.n_strx);
            n_type  = nlist.n_type;
            n_sect  = nlist.n_sect;
            n_value = macho_swap64(ctx, nlist.n_value);
        } else {
            MachONlist32 nlist;
            if (!safe_read_bytes(ctx->data, ctx->size, entry_off, &nlist, sizeof(nlist))) {
                if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_OFFSET, entry_off,
                                        "Cannot read nlist32 entry %u at 0x%zX", i, entry_off);
                break;
            }
            n_strx  = macho_swap32(ctx, nlist.n_strx);
            n_type  = nlist.n_type;
            n_sect  = nlist.n_sect;
            n_value = macho_swap32(ctx, nlist.n_value);
        }

        SymbolEntry *sym = &result->symbols[result->count];

        /* Extract name from string table */
        if (n_strx < strsize) {
            size_t name_offset = (size_t)stroff + (size_t)n_strx;
            const char *name = safe_read_string(ctx->data, ctx->size,
                                                name_offset, MACHO_MAX_SYMBOL_NAME);
            if (name) {
                sym->name = strdup(name);
            } else {
                sym->name = strdup("<invalid>");
            }
        } else {
            sym->name = strdup("<out_of_range>");
            if (diags) diag_add_fmt(diags, DIAG_WARN_PARTIAL_METADATA, entry_off,
                                    "Symbol %u string index 0x%X exceeds string table size 0x%X",
                                    i, n_strx, strsize);
        }

        if (!sym->name) {
            if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                                "Failed to allocate symbol name");
            /* Free already-allocated names */
            for (size_t k = 0; k < result->count; k++)
                free(result->symbols[k].name);
            free(result->symbols);
            result->symbols = NULL;
            result->count = 0;
            return DIAG_ERR_ALLOC_FAILED;
        }

        sym->value      = n_value;
        sym->section    = n_sect;
        sym->type       = classify_ntype(n_type);
        sym->visibility = classify_visibility(n_type);
        sym->is_debug   = (n_type & MACHO_N_STAB) != 0;

        /* Update counters */
        switch (sym->visibility) {
            case SYM_VIS_LOCAL:            local_count++;    break;
            case SYM_VIS_EXTERNAL:         external_count++; break;
            case SYM_VIS_PRIVATE_EXTERNAL: external_count++; break;
        }
        if (sym->type == SYM_UNDEFINED)
            undefined_count++;

        result->count++;
    }

    result->local_count     = local_count;
    result->external_count  = external_count;
    result->undefined_count = undefined_count;

    return DIAG_OK;
}

void symbol_table_destroy(SymbolTable *table) {
    if (!table) return;
    for (size_t i = 0; i < table->count; i++) {
        free(table->symbols[i].name);
    }
    free(table->symbols);
    memset(table, 0, sizeof(SymbolTable));
}

const char *symbol_type_name(SymbolType type) {
    switch (type) {
        case SYM_UNDEFINED: return "undefined";
        case SYM_ABSOLUTE:  return "absolute";
        case SYM_SECTION:   return "section";
        case SYM_PREBOUND:  return "prebound";
        case SYM_INDIRECT:  return "indirect";
        case SYM_DEBUG:     return "debug";
        default:            return "unknown";
    }
}
