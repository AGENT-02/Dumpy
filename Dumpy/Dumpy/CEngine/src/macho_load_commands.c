#include "macho_load_commands.h"
#include "macho_types.h"
#include "safe_read.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

const char *load_command_name(uint32_t cmd) {
    switch (cmd) {
        case LC_SEGMENT:               return "LC_SEGMENT";
        case LC_SYMTAB:                return "LC_SYMTAB";
        case 0x3:                      return "LC_SYMSEG";
        case LC_THREAD:                return "LC_THREAD";
        case LC_UNIXTHREAD:            return "LC_UNIXTHREAD";
        case 0x6:                      return "LC_LOADFVMLIB";
        case 0x7:                      return "LC_IDFVMLIB";
        case 0x8:                      return "LC_IDENT";
        case 0x9:                      return "LC_FVMFILE";
        case 0xA:                      return "LC_PREPAGE";
        case LC_DYSYMTAB:              return "LC_DYSYMTAB";
        case LC_LOAD_DYLIB:            return "LC_LOAD_DYLIB";
        case LC_ID_DYLIB:              return "LC_ID_DYLIB";
        case LC_LOAD_DYLINKER:         return "LC_LOAD_DYLINKER";
        case LC_ID_DYLINKER:           return "LC_ID_DYLINKER";
        case LC_PREBOUND_DYLIB:        return "LC_PREBOUND_DYLIB";
        case LC_ROUTINES:              return "LC_ROUTINES";
        case LC_SUB_FRAMEWORK:         return "LC_SUB_FRAMEWORK";
        case LC_SUB_UMBRELLA:          return "LC_SUB_UMBRELLA";
        case LC_SUB_CLIENT:            return "LC_SUB_CLIENT";
        case LC_SUB_LIBRARY:           return "LC_SUB_LIBRARY";
        case LC_TWOLEVEL_HINTS:        return "LC_TWOLEVEL_HINTS";
        case LC_PREBIND_CKSUM:         return "LC_PREBIND_CKSUM";
        case LC_LOAD_WEAK_DYLIB:       return "LC_LOAD_WEAK_DYLIB";
        case LC_SEGMENT_64:            return "LC_SEGMENT_64";
        case LC_ROUTINES_64:           return "LC_ROUTINES_64";
        case LC_UUID:                  return "LC_UUID";
        case LC_RPATH:                 return "LC_RPATH";
        case LC_CODE_SIGNATURE:        return "LC_CODE_SIGNATURE";
        case LC_SEGMENT_SPLIT_INFO:    return "LC_SEGMENT_SPLIT_INFO";
        case LC_REEXPORT_DYLIB:        return "LC_REEXPORT_DYLIB";
        case LC_LAZY_LOAD_DYLIB:       return "LC_LAZY_LOAD_DYLIB";
        case LC_ENCRYPTION_INFO:       return "LC_ENCRYPTION_INFO";
        case LC_DYLD_INFO:             return "LC_DYLD_INFO";
        case LC_DYLD_INFO_ONLY:        return "LC_DYLD_INFO_ONLY";
        case LC_LOAD_UPWARD_DYLIB:     return "LC_LOAD_UPWARD_DYLIB";
        case LC_VERSION_MIN_MACOSX:    return "LC_VERSION_MIN_MACOSX";
        case LC_VERSION_MIN_IPHONEOS:  return "LC_VERSION_MIN_IPHONEOS";
        case LC_FUNCTION_STARTS:       return "LC_FUNCTION_STARTS";
        case LC_DYLD_ENVIRONMENT:      return "LC_DYLD_ENVIRONMENT";
        case LC_MAIN:                  return "LC_MAIN";
        case LC_DATA_IN_CODE:          return "LC_DATA_IN_CODE";
        case LC_SOURCE_VERSION:        return "LC_SOURCE_VERSION";
        case LC_DYLIB_CODE_SIGN_DRS:   return "LC_DYLIB_CODE_SIGN_DRS";
        case LC_ENCRYPTION_INFO_64:    return "LC_ENCRYPTION_INFO_64";
        case LC_LINKER_OPTION:         return "LC_LINKER_OPTION";
        case LC_LINKER_OPTIMIZATION_HINT: return "LC_LINKER_OPTIMIZATION_HINT";
        case LC_VERSION_MIN_TVOS:      return "LC_VERSION_MIN_TVOS";
        case LC_VERSION_MIN_WATCHOS:   return "LC_VERSION_MIN_WATCHOS";
        case LC_NOTE:                  return "LC_NOTE";
        case LC_BUILD_VERSION:         return "LC_BUILD_VERSION";
        case LC_DYLD_EXPORTS_TRIE:     return "LC_DYLD_EXPORTS_TRIE";
        case LC_DYLD_CHAINED_FIXUPS:   return "LC_DYLD_CHAINED_FIXUPS";
        default:                       return "LC_UNKNOWN";
    }
}

/* Format a Mach-O packed version (xxxx.yy.zz) to a human-readable string. */
static char *format_version(uint32_t ver) {
    uint32_t major = (ver >> 16) & 0xFFFF;
    uint32_t minor = (ver >> 8) & 0xFF;
    uint32_t patch = ver & 0xFF;

    char buf[64];
    if (patch == 0) {
        snprintf(buf, sizeof(buf), "%u.%u", major, minor);
    } else {
        snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, patch);
    }
    return strdup(buf);
}

/* Format a source version (A.B.C.D.E packed in 64 bits). */
static char *format_source_version(uint64_t ver) {
    uint64_t a = (ver >> 40) & 0xFFFFFF;
    uint64_t b = (ver >> 30) & 0x3FF;
    uint64_t c = (ver >> 20) & 0x3FF;
    uint64_t d = (ver >> 10) & 0x3FF;
    uint64_t e = ver & 0x3FF;

    char buf[128];
    snprintf(buf, sizeof(buf), "%llu.%llu.%llu.%llu.%llu",
             (unsigned long long)a, (unsigned long long)b,
             (unsigned long long)c, (unsigned long long)d,
             (unsigned long long)e);
    return strdup(buf);
}

DiagCode macho_parse_load_commands(const MachOContext *ctx,
                                   LoadCommandsInfo *info,
                                   DiagList *diags) {
    if (!ctx || !info) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                            "NULL argument to macho_parse_load_commands");
        return DIAG_ERR_TRUNCATED;
    }

    memset(info, 0, sizeof(LoadCommandsInfo));

    /* Read header to get ncmds and sizeofcmds */
    uint32_t ncmds, sizeofcmds;

    if (ctx->is_64bit) {
        MachOHeader64 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr))) {
            if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                                "Cannot read header for load commands");
            return DIAG_ERR_TRUNCATED;
        }
        ncmds      = macho_swap32(ctx, hdr.ncmds);
        sizeofcmds = macho_swap32(ctx, hdr.sizeofcmds);
    } else {
        MachOHeader32 hdr;
        if (!safe_read_bytes(ctx->data, ctx->size, 0, &hdr, sizeof(hdr))) {
            if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, 0,
                                "Cannot read header for load commands");
            return DIAG_ERR_TRUNCATED;
        }
        ncmds      = macho_swap32(ctx, hdr.ncmds);
        sizeofcmds = macho_swap32(ctx, hdr.sizeofcmds);
    }

    /* Validate load commands region */
    size_t lc_start = ctx->header_size;
    if (!safe_check_range(ctx->size, lc_start, sizeofcmds)) {
        if (diags) diag_add(diags, DIAG_ERR_TRUNCATED, lc_start,
                            "Load commands region extends beyond file");
        return DIAG_ERR_TRUNCATED;
    }

    /* Cap ncmds to avoid absurd allocations */
    if (ncmds > 100000) {
        if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_LOAD_COMMAND, lc_start,
                                "Unreasonable number of load commands: %u", ncmds);
        return DIAG_ERR_INVALID_LOAD_COMMAND;
    }

    info->commands = (LoadCommandEntry *)calloc(ncmds, sizeof(LoadCommandEntry));
    if (!info->commands) {
        if (diags) diag_add(diags, DIAG_ERR_ALLOC_FAILED, 0,
                            "Failed to allocate load command entries");
        return DIAG_ERR_ALLOC_FAILED;
    }

    size_t offset = lc_start;
    size_t lc_end = lc_start + sizeofcmds;
    size_t cmd_index = 0;

    for (uint32_t i = 0; i < ncmds; i++) {
        /* Read the generic load command header */
        MachOLoadCommand lc;
        if (!safe_read_bytes(ctx->data, ctx->size, offset, &lc, sizeof(lc))) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_TRUNCATED, offset,
                                    "Cannot read load command %u at offset 0x%zX", i, offset);
            break;
        }

        uint32_t cmd     = macho_swap32(ctx, lc.cmd);
        uint32_t cmdsize = macho_swap32(ctx, lc.cmdsize);

        /* Validate cmdsize */
        if (cmdsize < sizeof(MachOLoadCommand)) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_LOAD_COMMAND, offset,
                                    "Load command %u has invalid cmdsize %u", i, cmdsize);
            break;
        }

        if (offset + cmdsize > lc_end) {
            if (diags) diag_add_fmt(diags, DIAG_ERR_INVALID_LOAD_COMMAND, offset,
                                    "Load command %u (size %u) extends beyond load commands region",
                                    i, cmdsize);
            break;
        }

        /* Store entry */
        info->commands[cmd_index].cmd      = cmd;
        info->commands[cmd_index].cmdsize  = cmdsize;
        info->commands[cmd_index].offset   = offset;
        info->commands[cmd_index].cmd_name = load_command_name(cmd);
        cmd_index++;

        /* Validate cmdsize alignment */
        if (cmdsize % 4 != 0) {
            if (diags) diag_add_fmt(diags, DIAG_WARN_ALIGNMENT, offset,
                                    "Load command %u cmdsize %u is not 4-byte aligned", i, cmdsize);
        }

        /* Extract specific load command data */
        switch (cmd) {
            case LC_UUID: {
                if (cmdsize >= sizeof(MachOUUIDCommand)) {
                    MachOUUIDCommand uuid_cmd;
                    if (safe_read_bytes(ctx->data, ctx->size, offset,
                                        &uuid_cmd, sizeof(uuid_cmd))) {
                        memcpy(info->uuid, uuid_cmd.uuid, 16);
                        info->has_uuid = true;
                    }
                }
                break;
            }

            case LC_BUILD_VERSION: {
                if (cmdsize >= sizeof(MachOBuildVersionCommand)) {
                    MachOBuildVersionCommand bv;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &bv, sizeof(bv))) {
                        uint32_t plat  = macho_swap32(ctx, bv.platform);
                        uint32_t minos = macho_swap32(ctx, bv.minos);
                        uint32_t sdk   = macho_swap32(ctx, bv.sdk);
                        uint32_t ntools = macho_swap32(ctx, bv.ntools);
                        info->platform = plat;
                        free(info->min_version_string);
                        info->min_version_string = format_version(minos);
                        free(info->sdk_version_string);
                        info->sdk_version_string = format_version(sdk);

                        /* Parse build tool versions */
                        size_t tools_offset = offset + sizeof(MachOBuildVersionCommand);
                        if (ntools > 8) ntools = 8; /* cap to fixed array size */
                        for (uint32_t t = 0; t < ntools; t++) {
                            MachOBuildToolVersion btv;
                            size_t btv_off = tools_offset + (size_t)t * sizeof(MachOBuildToolVersion);
                            if (btv_off + sizeof(MachOBuildToolVersion) > offset + cmdsize) break;
                            if (!safe_read_bytes(ctx->data, ctx->size, btv_off, &btv, sizeof(btv))) break;
                            uint32_t tool = macho_swap32(ctx, btv.tool);
                            uint32_t ver  = macho_swap32(ctx, btv.version);
                            const char *tool_name;
                            switch (tool) {
                                case 1:  tool_name = "clang"; break;
                                case 2:  tool_name = "swift"; break;
                                case 3:  tool_name = "ld";    break;
                                case 4:  tool_name = "lld";   break;
                                default: tool_name = "unknown"; break;
                            }
                            info->build_tool_names[info->build_tool_count] = strdup(tool_name);
                            info->build_tool_versions[info->build_tool_count] = format_version(ver);
                            info->build_tool_count++;
                        }
                    }
                }
                break;
            }

            case LC_VERSION_MIN_IPHONEOS:
            case LC_VERSION_MIN_MACOSX:
            case LC_VERSION_MIN_TVOS:
            case LC_VERSION_MIN_WATCHOS: {
                if (cmdsize >= sizeof(MachOVersionMinCommand)) {
                    MachOVersionMinCommand vm;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &vm, sizeof(vm))) {
                        uint32_t ver = macho_swap32(ctx, vm.version);
                        uint32_t sdk = macho_swap32(ctx, vm.sdk);
                        free(info->min_version_string);
                        info->min_version_string = format_version(ver);
                        free(info->sdk_version_string);
                        info->sdk_version_string = format_version(sdk);
                        /* Map LC type to platform */
                        if (!info->platform) {
                            switch (cmd) {
                                case LC_VERSION_MIN_IPHONEOS: info->platform = PLATFORM_IOS; break;
                                case LC_VERSION_MIN_MACOSX:   info->platform = PLATFORM_MACOS; break;
                                case LC_VERSION_MIN_TVOS:     info->platform = PLATFORM_TVOS; break;
                                case LC_VERSION_MIN_WATCHOS:  info->platform = PLATFORM_WATCHOS; break;
                            }
                        }
                    }
                }
                break;
            }

            case LC_SOURCE_VERSION: {
                if (cmdsize >= sizeof(MachOSourceVersionCommand)) {
                    MachOSourceVersionCommand sv;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &sv, sizeof(sv))) {
                        uint64_t ver = macho_swap64(ctx, sv.version);
                        free(info->source_version_string);
                        info->source_version_string = format_source_version(ver);
                    }
                }
                break;
            }

            case LC_MAIN: {
                if (cmdsize >= sizeof(MachOEntryPointCommand)) {
                    MachOEntryPointCommand ep;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &ep, sizeof(ep))) {
                        info->entry_point_offset = macho_swap64(ctx, ep.entryoff);
                        info->has_entry_point = true;
                    }
                }
                break;
            }

            case LC_LOAD_DYLIB:
            case LC_LOAD_WEAK_DYLIB:
            case LC_LAZY_LOAD_DYLIB:
            case LC_REEXPORT_DYLIB: {
                if (cmdsize >= sizeof(MachODylibCommand)) {
                    MachODylibCommand dylib_cmd;
                    if (safe_read_bytes(ctx->data, ctx->size, offset,
                                        &dylib_cmd, sizeof(dylib_cmd))) {
                        uint32_t name_off = macho_swap32(ctx, dylib_cmd.dylib.name_offset);
                        if (name_off < cmdsize && info->dylib_count < 10000) {
                            size_t max_len = cmdsize - name_off;
                            const char *src = (const char *)ctx->data + offset + name_off;
                            size_t slen = strnlen(src, max_len);
                            char *name = (char *)malloc(slen + 1);
                            if (name) {
                                memcpy(name, src, slen);
                                name[slen] = '\0';
                                char **tmp = (char **)realloc(info->dylib_names,
                                    (info->dylib_count + 1) * sizeof(char *));
                                if (tmp) {
                                    info->dylib_names = tmp;
                                    info->dylib_names[info->dylib_count++] = name;
                                } else {
                                    free(name);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case LC_RPATH: {
                if (cmdsize >= sizeof(MachORpathCommand)) {
                    MachORpathCommand rpath_cmd;
                    if (safe_read_bytes(ctx->data, ctx->size, offset,
                                        &rpath_cmd, sizeof(rpath_cmd))) {
                        uint32_t path_off = macho_swap32(ctx, rpath_cmd.path_offset);
                        if (path_off < cmdsize && info->rpath_count < 1000) {
                            size_t max_len = cmdsize - path_off;
                            const char *src = (const char *)ctx->data + offset + path_off;
                            size_t slen = strnlen(src, max_len);
                            char *path = (char *)malloc(slen + 1);
                            if (path) {
                                memcpy(path, src, slen);
                                path[slen] = '\0';
                                char **tmp = (char **)realloc(info->rpaths,
                                    (info->rpath_count + 1) * sizeof(char *));
                                if (tmp) {
                                    info->rpaths = tmp;
                                    info->rpaths[info->rpath_count++] = path;
                                } else {
                                    free(path);
                                }
                            }
                        }
                    }
                }
                break;
            }

            case LC_SYMTAB: {
                if (cmdsize >= sizeof(MachOSymtabCommand)) {
                    MachOSymtabCommand symtab;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &symtab, sizeof(symtab))) {
                        info->symtab_symoff  = macho_swap32(ctx, symtab.symoff);
                        info->symtab_nsyms   = macho_swap32(ctx, symtab.nsyms);
                        info->symtab_stroff  = macho_swap32(ctx, symtab.stroff);
                        info->symtab_strsize = macho_swap32(ctx, symtab.strsize);
                        info->has_symtab = true;
                    }
                }
                break;
            }

            case LC_DYSYMTAB: {
                if (cmdsize >= sizeof(MachODysymtabCommand)) {
                    MachODysymtabCommand dysymtab;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &dysymtab, sizeof(dysymtab))) {
                        info->dysymtab_ilocalsym      = macho_swap32(ctx, dysymtab.ilocalsym);
                        info->dysymtab_nlocalsym      = macho_swap32(ctx, dysymtab.nlocalsym);
                        info->dysymtab_iextdefsym     = macho_swap32(ctx, dysymtab.iextdefsym);
                        info->dysymtab_nextdefsym     = macho_swap32(ctx, dysymtab.nextdefsym);
                        info->dysymtab_iundefsym      = macho_swap32(ctx, dysymtab.iundefsym);
                        info->dysymtab_nundefsym      = macho_swap32(ctx, dysymtab.nundefsym);
                        info->dysymtab_indirectsymoff  = macho_swap32(ctx, dysymtab.indirectsymoff);
                        info->dysymtab_nindirectsyms   = macho_swap32(ctx, dysymtab.nindirectsyms);
                        info->has_dysymtab = true;
                    }
                }
                break;
            }

            case LC_CODE_SIGNATURE: {
                if (cmdsize >= sizeof(MachOLinkeditDataCommand)) {
                    MachOLinkeditDataCommand cs;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &cs, sizeof(cs))) {
                        info->code_sig_offset = macho_swap32(ctx, cs.dataoff);
                        info->code_sig_size   = macho_swap32(ctx, cs.datasize);
                        info->has_code_signature = true;
                    }
                }
                break;
            }

            case LC_ENCRYPTION_INFO: {
                if (cmdsize >= sizeof(MachOEncryptionInfoCommand)) {
                    MachOEncryptionInfoCommand ei;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &ei, sizeof(ei))) {
                        info->crypt_offset = macho_swap32(ctx, ei.cryptoff);
                        info->crypt_size   = macho_swap32(ctx, ei.cryptsize);
                        info->crypt_id     = macho_swap32(ctx, ei.cryptid);
                        info->is_encrypted = (info->crypt_id != 0);
                    }
                }
                break;
            }

            case LC_ENCRYPTION_INFO_64: {
                if (cmdsize >= sizeof(MachOEncryptionInfoCommand64)) {
                    MachOEncryptionInfoCommand64 ei64;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &ei64, sizeof(ei64))) {
                        info->crypt_offset = macho_swap32(ctx, ei64.cryptoff);
                        info->crypt_size   = macho_swap32(ctx, ei64.cryptsize);
                        info->crypt_id     = macho_swap32(ctx, ei64.cryptid);
                        info->is_encrypted = (info->crypt_id != 0);
                    }
                }
                break;
            }

            case LC_FUNCTION_STARTS: {
                if (cmdsize >= sizeof(MachOLinkeditDataCommand)) {
                    MachOLinkeditDataCommand fs;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &fs, sizeof(fs))) {
                        info->function_starts_offset = macho_swap32(ctx, fs.dataoff);
                        info->function_starts_size   = macho_swap32(ctx, fs.datasize);
                        info->has_function_starts = true;
                    }
                }
                break;
            }

            case LC_DATA_IN_CODE: {
                if (cmdsize >= sizeof(MachOLinkeditDataCommand)) {
                    MachOLinkeditDataCommand dic;
                    if (safe_read_bytes(ctx->data, ctx->size, offset, &dic, sizeof(dic))) {
                        info->data_in_code_offset = macho_swap32(ctx, dic.dataoff);
                        info->data_in_code_size   = macho_swap32(ctx, dic.datasize);
                        info->has_data_in_code = true;
                    }
                }
                break;
            }

            default:
                break;
        }

        offset += cmdsize;
    }

    info->count = cmd_index;

    /* Detect code signature signing type from superblob header */
    if (info->has_code_signature && info->code_sig_size >= 8) {
        uint32_t cs_magic = 0;
        if (safe_read_bytes(ctx->data, ctx->size,
                            (size_t)info->code_sig_offset, &cs_magic, 4)) {
            /* Code signature superblob uses big-endian magic */
            uint32_t magic_be = ((cs_magic >> 24) & 0xFF)
                              | (((cs_magic >> 16) & 0xFF) << 8)
                              | (((cs_magic >> 8)  & 0xFF) << 16)
                              | ((cs_magic & 0xFF) << 24);
            if (cs_magic == 0xFADE0CC0 || magic_be == 0xFADE0CC0) {
                /* It's a valid superblob - check for ad-hoc by looking
                   for a CMS signature blob (magic 0xFADE0B01).
                   If there are only hash slots but no CMS blob, it's ad-hoc. */
                uint32_t blob_count = 0;
                if (safe_read_bytes(ctx->data, ctx->size,
                                    (size_t)info->code_sig_offset + 4,
                                    &blob_count, 4)) {
                    /* blob_count is big-endian */
                    uint32_t count = ((blob_count >> 24) & 0xFF)
                                   | (((blob_count >> 16) & 0xFF) << 8)
                                   | (((blob_count >> 8)  & 0xFF) << 16)
                                   | ((blob_count & 0xFF) << 24);
                    bool found_cms = false;
                    for (uint32_t bi = 0; bi < count && bi < 20; bi++) {
                        /* Each blob index: type (4 bytes) + offset (4 bytes) */
                        size_t idx_off = (size_t)info->code_sig_offset + 8 + (size_t)bi * 8;
                        uint32_t blob_type = 0;
                        uint32_t blob_off = 0;
                        if (!safe_read_bytes(ctx->data, ctx->size, idx_off, &blob_type, 4))
                            break;
                        if (!safe_read_bytes(ctx->data, ctx->size, idx_off + 4, &blob_off, 4))
                            break;
                        /* Convert from big-endian */
                        uint32_t btype = ((blob_type >> 24) & 0xFF)
                                       | (((blob_type >> 16) & 0xFF) << 8)
                                       | (((blob_type >> 8)  & 0xFF) << 16)
                                       | ((blob_type & 0xFF) << 24);
                        uint32_t boff = ((blob_off >> 24) & 0xFF)
                                      | (((blob_off >> 16) & 0xFF) << 8)
                                      | (((blob_off >> 8)  & 0xFF) << 16)
                                      | ((blob_off & 0xFF) << 24);
                        /* Check if this blob is a CMS signature (type 0x10000) */
                        if (btype == 0x10000) {
                            /* Read the blob magic to confirm */
                            uint32_t blob_magic = 0;
                            size_t abs_off = (size_t)info->code_sig_offset + boff;
                            if (safe_read_bytes(ctx->data, ctx->size, abs_off, &blob_magic, 4)) {
                                uint32_t bm = ((blob_magic >> 24) & 0xFF)
                                            | (((blob_magic >> 16) & 0xFF) << 8)
                                            | (((blob_magic >> 8)  & 0xFF) << 16)
                                            | ((blob_magic & 0xFF) << 24);
                                if (bm == 0xFADE0B01) {
                                    /* Check if the CMS blob has actual content (size > 8) */
                                    uint32_t blob_size = 0;
                                    if (safe_read_bytes(ctx->data, ctx->size, abs_off + 4, &blob_size, 4)) {
                                        uint32_t bs = ((blob_size >> 24) & 0xFF)
                                                    | (((blob_size >> 16) & 0xFF) << 8)
                                                    | (((blob_size >> 8)  & 0xFF) << 16)
                                                    | ((blob_size & 0xFF) << 24);
                                        if (bs > 8) {
                                            found_cms = true;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    info->signing_status = found_cms ? 1 : 2; /* 1=signed, 2=ad-hoc */
                }
            }
        }
    }

    /* Check for overlapping load commands in the header region */
    for (size_t a = 0; a < cmd_index; a++) {
        size_t a_start = info->commands[a].offset;
        size_t a_end   = a_start + info->commands[a].cmdsize;
        for (size_t b = a + 1; b < cmd_index; b++) {
            size_t b_start = info->commands[b].offset;
            size_t b_end   = b_start + info->commands[b].cmdsize;
            if (a_start < b_end && b_start < a_end) {
                if (diags) diag_add_fmt(diags, DIAG_WARN_ALIGNMENT, a_start,
                    "Load commands %zu (%s) and %zu (%s) overlap in header region",
                    a, info->commands[a].cmd_name,
                    b, info->commands[b].cmd_name);
            }
        }
    }

    return DIAG_OK;
}

void load_commands_info_destroy(LoadCommandsInfo *info) {
    if (!info) return;
    free(info->commands);
    info->commands = NULL;
    free(info->min_version_string);
    info->min_version_string = NULL;
    free(info->sdk_version_string);
    info->sdk_version_string = NULL;
    free(info->source_version_string);
    info->source_version_string = NULL;

    for (size_t i = 0; i < info->dylib_count; i++) {
        free(info->dylib_names[i]);
    }
    free(info->dylib_names);
    info->dylib_names = NULL;
    info->dylib_count = 0;

    for (size_t i = 0; i < info->rpath_count; i++) {
        free(info->rpaths[i]);
    }
    free(info->rpaths);
    info->rpaths = NULL;
    info->rpath_count = 0;

    for (size_t i = 0; i < info->build_tool_count; i++) {
        free(info->build_tool_names[i]);
        free(info->build_tool_versions[i]);
        info->build_tool_names[i] = NULL;
        info->build_tool_versions[i] = NULL;
    }
    info->build_tool_count = 0;

    info->count = 0;
}
