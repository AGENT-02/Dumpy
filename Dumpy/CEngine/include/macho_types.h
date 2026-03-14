#ifndef MACHO_TYPES_H
#define MACHO_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ---- Magic numbers ---- */
#define MACHO_MAGIC_32    0xFEEDFACE
#define MACHO_MAGIC_64    0xFEEDFACF
#define MACHO_CIGAM_32    0xCEFAEDFE
#define MACHO_CIGAM_64    0xCFFAEDFE
#define MACHO_FAT_MAGIC   0xCAFEBABE
#define MACHO_FAT_CIGAM   0xBEBAFECA

/* ---- CPU types ---- */
#define MACHO_CPU_ARCH_ABI64    0x01000000
#define MACHO_CPU_TYPE_ARM      12
#define MACHO_CPU_TYPE_ARM64    (MACHO_CPU_TYPE_ARM | MACHO_CPU_ARCH_ABI64)
#define MACHO_CPU_TYPE_X86      7
#define MACHO_CPU_TYPE_X86_64   (MACHO_CPU_TYPE_X86 | MACHO_CPU_ARCH_ABI64)

/* ---- CPU subtypes ---- */
#define MACHO_CPU_SUBTYPE_ARM64_ALL  0
#define MACHO_CPU_SUBTYPE_ARM64E     2

/* ---- File types ---- */
#define MACHO_FILETYPE_OBJECT    1
#define MACHO_FILETYPE_EXECUTE   2
#define MACHO_FILETYPE_FVMLIB    3
#define MACHO_FILETYPE_CORE      4
#define MACHO_FILETYPE_PRELOAD   5
#define MACHO_FILETYPE_DYLIB     6
#define MACHO_FILETYPE_DYLINKER  7
#define MACHO_FILETYPE_BUNDLE    8
#define MACHO_FILETYPE_DYLIB_STUB 9
#define MACHO_FILETYPE_DSYM      10
#define MACHO_FILETYPE_KEXT      11
#define MACHO_FILETYPE_FILESET   12

/* ---- Load command types ---- */
#define LC_REQ_DYLD              0x80000000

#define LC_SEGMENT               0x1
#define LC_SYMTAB                0x2
#define LC_THREAD                0x4
#define LC_UNIXTHREAD            0x5
#define LC_DYSYMTAB              0xB
#define LC_LOAD_DYLIB            0xC
#define LC_ID_DYLIB              0xD
#define LC_LOAD_DYLINKER         0xE
#define LC_ID_DYLINKER           0xF
#define LC_PREBOUND_DYLIB        0x10
#define LC_ROUTINES              0x11
#define LC_SUB_FRAMEWORK         0x12
#define LC_SUB_UMBRELLA          0x13
#define LC_SUB_CLIENT            0x14
#define LC_SUB_LIBRARY           0x15
#define LC_TWOLEVEL_HINTS        0x16
#define LC_PREBIND_CKSUM         0x17
#define LC_LOAD_WEAK_DYLIB       (0x18 | LC_REQ_DYLD)
#define LC_SEGMENT_64            0x19
#define LC_ROUTINES_64           0x1A
#define LC_UUID                  0x1B
#define LC_RPATH                 (0x1C | LC_REQ_DYLD)
#define LC_CODE_SIGNATURE        0x1D
#define LC_SEGMENT_SPLIT_INFO    0x1E
#define LC_REEXPORT_DYLIB        (0x1F | LC_REQ_DYLD)
#define LC_LAZY_LOAD_DYLIB       0x20
#define LC_ENCRYPTION_INFO       0x21
#define LC_DYLD_INFO             0x22
#define LC_DYLD_INFO_ONLY        (0x22 | LC_REQ_DYLD)
#define LC_LOAD_UPWARD_DYLIB     (0x23 | LC_REQ_DYLD)
#define LC_VERSION_MIN_MACOSX    0x24
#define LC_VERSION_MIN_IPHONEOS  0x25
#define LC_FUNCTION_STARTS       0x26
#define LC_DYLD_ENVIRONMENT      0x27
#define LC_MAIN                  (0x28 | LC_REQ_DYLD)
#define LC_DATA_IN_CODE          0x29
#define LC_SOURCE_VERSION        0x2A
#define LC_DYLIB_CODE_SIGN_DRS   0x2B
#define LC_ENCRYPTION_INFO_64    0x2C
#define LC_LINKER_OPTION         0x2D
#define LC_LINKER_OPTIMIZATION_HINT 0x2E
#define LC_VERSION_MIN_TVOS      0x2F
#define LC_VERSION_MIN_WATCHOS   0x30
#define LC_NOTE                  0x31
#define LC_BUILD_VERSION         0x32
#define LC_DYLD_EXPORTS_TRIE     (0x33 | LC_REQ_DYLD)
#define LC_DYLD_CHAINED_FIXUPS  (0x34 | LC_REQ_DYLD)

/* ---- Section type masks ---- */
#define SECTION_TYPE_MASK              0x000000FF
#define SECTION_ATTRIBUTES_MASK        0xFFFFFF00
#define S_REGULAR                      0x0
#define S_ZEROFILL                     0x1
#define S_CSTRING_LITERALS             0x2
#define S_4BYTE_LITERALS               0x3
#define S_8BYTE_LITERALS               0x4
#define S_LITERAL_POINTERS             0x5
#define S_NON_LAZY_SYMBOL_POINTERS     0x6
#define S_LAZY_SYMBOL_POINTERS         0x7
#define S_SYMBOL_STUBS                 0x8
#define S_MOD_INIT_FUNC_POINTERS       0x9
#define S_MOD_TERM_FUNC_POINTERS       0xA
#define S_COALESCED                    0xB
#define S_GB_ZEROFILL                  0xC
#define S_INTERPOSING                  0xD
#define S_16BYTE_LITERALS              0xE
#define S_DTRACE_DOF                   0xF
#define S_THREAD_LOCAL_REGULAR         0x11
#define S_THREAD_LOCAL_ZEROFILL        0x12
#define S_THREAD_LOCAL_VARIABLES       0x13
#define S_THREAD_LOCAL_VARIABLE_POINTERS 0x14
#define S_THREAD_LOCAL_INIT_FUNCTION_POINTERS 0x15

/* ---- VM protection flags ---- */
#define VM_PROT_READ     0x01
#define VM_PROT_WRITE    0x02
#define VM_PROT_EXECUTE  0x04

/* ---- N_TYPE masks for nlist ---- */
#define MACHO_N_STAB  0xE0
#define MACHO_N_PEXT  0x10
#define MACHO_N_TYPE  0x0E
#define MACHO_N_EXT   0x01
#define MACHO_N_UNDF  0x0
#define MACHO_N_ABS   0x2
#define MACHO_N_SECT  0xE
#define MACHO_N_PBUD  0xC
#define MACHO_N_INDR  0xA

/* ---- Platform constants for LC_BUILD_VERSION ---- */
#define PLATFORM_MACOS     1
#define PLATFORM_IOS       2
#define PLATFORM_TVOS      3
#define PLATFORM_WATCHOS   4
#define PLATFORM_BRIDGEOS  5
#define PLATFORM_MACCATALYST 6
#define PLATFORM_IOSSIMULATOR   7
#define PLATFORM_TVOSSIMULATOR  8
#define PLATFORM_WATCHOSSIMULATOR 9
#define PLATFORM_DRIVERKIT  10
#define PLATFORM_VISIONOS   11

/* ================================================================ */
/* Portable packed structure definitions                            */
/* ================================================================ */

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t nfat_arch;
} MachOFatHeader;

typedef struct __attribute__((packed)) {
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t offset;
    uint32_t size;
    uint32_t align;
} MachOFatArch;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
} MachOHeader32;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t cputype;
    uint32_t cpusubtype;
    uint32_t filetype;
    uint32_t ncmds;
    uint32_t sizeofcmds;
    uint32_t flags;
    uint32_t reserved;
} MachOHeader64;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
} MachOLoadCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint32_t vmaddr;
    uint32_t vmsize;
    uint32_t fileoff;
    uint32_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} MachOSegmentCommand32;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    char     segname[16];
    uint64_t vmaddr;
    uint64_t vmsize;
    uint64_t fileoff;
    uint64_t filesize;
    uint32_t maxprot;
    uint32_t initprot;
    uint32_t nsects;
    uint32_t flags;
} MachOSegmentCommand64;

typedef struct __attribute__((packed)) {
    char     sectname[16];
    char     segname[16];
    uint32_t addr;
    uint32_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
} MachOSection32;

typedef struct __attribute__((packed)) {
    char     sectname[16];
    char     segname[16];
    uint64_t addr;
    uint64_t size;
    uint32_t offset;
    uint32_t align;
    uint32_t reloff;
    uint32_t nreloc;
    uint32_t flags;
    uint32_t reserved1;
    uint32_t reserved2;
    uint32_t reserved3;
} MachOSection64;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint8_t  uuid[16];
} MachOUUIDCommand;

typedef struct __attribute__((packed)) {
    uint32_t name_offset; /* offset from start of load command to string */
    uint32_t timestamp;
    uint32_t current_version;
    uint32_t compat_version;
} MachODylib;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    MachODylib dylib;
} MachODylibCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t symoff;
    uint32_t nsyms;
    uint32_t stroff;
    uint32_t strsize;
} MachOSymtabCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t ilocalsym;
    uint32_t nlocalsym;
    uint32_t iextdefsym;
    uint32_t nextdefsym;
    uint32_t iundefsym;
    uint32_t nundefsym;
    uint32_t tocoff;
    uint32_t ntoc;
    uint32_t modtaboff;
    uint32_t nmodtab;
    uint32_t extrefsymoff;
    uint32_t nextrefsyms;
    uint32_t indirectsymoff;
    uint32_t nindirectsyms;
    uint32_t extreloff;
    uint32_t nextrel;
    uint32_t locreloff;
    uint32_t nlocrel;
} MachODysymtabCommand;

typedef struct __attribute__((packed)) {
    uint32_t tool;
    uint32_t version;
} MachOBuildToolVersion;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t platform;
    uint32_t minos;
    uint32_t sdk;
    uint32_t ntools;
} MachOBuildVersionCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t version;
    uint32_t sdk;
} MachOVersionMinCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t version; /* A.B.C.D.E packed into 64 bits */
} MachOSourceVersionCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint64_t entryoff;
    uint64_t stacksize;
} MachOEntryPointCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t path_offset; /* offset from start of command to path string */
} MachORpathCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t dataoff;
    uint32_t datasize;
} MachOLinkeditDataCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t rebase_off;
    uint32_t rebase_size;
    uint32_t bind_off;
    uint32_t bind_size;
    uint32_t weak_bind_off;
    uint32_t weak_bind_size;
    uint32_t lazy_bind_off;
    uint32_t lazy_bind_size;
    uint32_t export_off;
    uint32_t export_size;
} MachODyldInfoCommand;

/* ---- nlist structures for symbol table ---- */

typedef struct __attribute__((packed)) {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    int16_t  n_desc;
    uint32_t n_value;
} MachONlist32;

typedef struct __attribute__((packed)) {
    uint32_t n_strx;
    uint8_t  n_type;
    uint8_t  n_sect;
    uint16_t n_desc;
    uint64_t n_value;
} MachONlist64;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t cryptoff;
    uint32_t cryptsize;
    uint32_t cryptid;
} MachOEncryptionInfoCommand;

typedef struct __attribute__((packed)) {
    uint32_t cmd;
    uint32_t cmdsize;
    uint32_t cryptoff;
    uint32_t cryptsize;
    uint32_t cryptid;
    uint32_t pad;
} MachOEncryptionInfoCommand64;

typedef struct __attribute__((packed)) {
    uint32_t offset;
    uint16_t length;
    uint16_t kind;
} MachODataInCodeEntry;

#endif /* MACHO_TYPES_H */
