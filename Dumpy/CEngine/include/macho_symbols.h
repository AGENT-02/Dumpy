#ifndef MACHO_SYMBOLS_H
#define MACHO_SYMBOLS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "diagnostics.h"
#include "macho_reader.h"

/* Symbol type classification */
typedef enum {
    SYM_UNDEFINED = 0,
    SYM_ABSOLUTE,
    SYM_SECTION,
    SYM_PREBOUND,
    SYM_INDIRECT,
    SYM_DEBUG
} SymbolType;

/* Symbol visibility */
typedef enum {
    SYM_VIS_LOCAL = 0,
    SYM_VIS_EXTERNAL,
    SYM_VIS_PRIVATE_EXTERNAL
} SymbolVisibility;

/* Parsed symbol entry */
typedef struct {
    char *name;
    uint64_t value;         /* Address or value */
    uint8_t section;        /* Section ordinal (1-based, 0 = NO_SECT) */
    SymbolType type;
    SymbolVisibility visibility;
    bool is_debug;
} SymbolEntry;

/* Symbol table result */
typedef struct {
    SymbolEntry *symbols;
    size_t count;
    size_t local_count;     /* Symbols with local visibility */
    size_t external_count;  /* Symbols with external visibility */
    size_t undefined_count; /* Undefined (imported) symbols */
} SymbolTable;

DiagCode macho_parse_symbols(
    const MachOContext *ctx,
    uint32_t symoff, uint32_t nsyms,
    uint32_t stroff, uint32_t strsize,
    SymbolTable *result, DiagList *diags);

void symbol_table_destroy(SymbolTable *table);

const char *symbol_type_name(SymbolType type);

#endif /* MACHO_SYMBOLS_H */
