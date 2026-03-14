#ifndef MACHO_LOAD_COMMANDS_H
#define MACHO_LOAD_COMMANDS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "macho_reader.h"
#include "diagnostics.h"

typedef struct {
    uint32_t cmd;
    uint32_t cmdsize;
    size_t offset; /* offset from start of file */
    const char *cmd_name; /* human-readable */
} LoadCommandEntry;

typedef struct {
    LoadCommandEntry *commands;
    size_t count;

    /* Extracted notable fields */
    uint8_t uuid[16];
    bool has_uuid;

    char *min_version_string;     /* heap-allocated, e.g. "15.0" */
    char *sdk_version_string;     /* heap-allocated */
    uint32_t platform;
    char *source_version_string;  /* heap-allocated */

    uint64_t entry_point_offset;
    bool has_entry_point;

    /* Linked dynamic libraries */
    char **dylib_names;
    size_t dylib_count;

    /* RPATHs */
    char **rpaths;
    size_t rpath_count;

    /* Code signature */
    uint32_t code_sig_offset;
    uint32_t code_sig_size;
    bool has_code_signature;

    /* Encryption info */
    bool is_encrypted;
    uint32_t crypt_offset;
    uint32_t crypt_size;
    uint32_t crypt_id;

    /* Function starts */
    uint32_t function_starts_offset;
    uint32_t function_starts_size;
    bool has_function_starts;

    /* Data in code */
    uint32_t data_in_code_offset;
    uint32_t data_in_code_size;
    bool has_data_in_code;

    /* Symbol table (LC_SYMTAB) */
    uint32_t symtab_symoff;
    uint32_t symtab_nsyms;
    uint32_t symtab_stroff;
    uint32_t symtab_strsize;
    bool has_symtab;

    /* Dynamic symbol table (LC_DYSYMTAB) */
    uint32_t dysymtab_ilocalsym;
    uint32_t dysymtab_nlocalsym;
    uint32_t dysymtab_iextdefsym;
    uint32_t dysymtab_nextdefsym;
    uint32_t dysymtab_iundefsym;
    uint32_t dysymtab_nundefsym;
    uint32_t dysymtab_indirectsymoff;
    uint32_t dysymtab_nindirectsyms;
    bool has_dysymtab;

    /* Code signature signing status:
       0 = unknown, 1 = signed, 2 = ad-hoc signed */
    int signing_status;

    /* Build tools from LC_BUILD_VERSION (max 8) */
    char *build_tool_names[8];     /* heap-allocated, e.g. "clang" */
    char *build_tool_versions[8];  /* heap-allocated, e.g. "15.0.0" */
    size_t build_tool_count;
} LoadCommandsInfo;

/// Parse all load commands from the Mach-O binary.
DiagCode macho_parse_load_commands(const MachOContext *ctx,
                                   LoadCommandsInfo *info,
                                   DiagList *diags);

/// Free all resources associated with a LoadCommandsInfo.
void load_commands_info_destroy(LoadCommandsInfo *info);

/// Return a human-readable name for a load command type constant.
const char *load_command_name(uint32_t cmd);

#endif /* MACHO_LOAD_COMMANDS_H */
