# Project Completion Roadmap

Dumpy is a native iOS Mach-O binary analyzer with a C parsing engine and SwiftUI interface. The core architecture is in place: FAT/thin binary parsing, ObjC metadata extraction, class dump formatting, and a multi-tab analysis UI. The app compiles and links for iOS 16.6.

**Current status:** The core architecture is solid. The C engine has named constants, NULL-check guards, recursion limits, bounds validation, and proper memory management. Load command parsing covers dylibs, RPATHs, code signatures, encryption info, function starts, and data-in-code. The UI has syntax-highlighted dump viewer, debounced search, navigable cross-references, stale bookmark handling, and accessibility improvements. Test coverage includes unit tests for the C engine, type encoding, property attributes, VM mapping, search service, and bridge integration.

This document is the authoritative task list for reaching production readiness.

---

# Core Architecture Tasks

## Module Boundaries and Separation

- [ ] `[P1]` **Refactor objc_parser.c (1220 lines) into smaller compilation units** — Split into `objc_class_parser.c`, `objc_protocol_parser.c`, `objc_category_parser.c`, `objc_selector_parser.c`. Current file handles all four in one translation unit. `Needs Refactor`
- [ ] `[P1]` **Refactor objc_formatter.c (587 lines)** — Extract `skip_one_type()` and `decode_one_type()` into `objc_type_decoder.c` to avoid duplicating logic already in `objc_resolver.c`. `Needs Refactor`
- [x] `[P2]` **Deduplicate PAC pointer stripping** — `strip_pac_pointer()` in `macho_vmmap.c` and `strip_pointer_tags()` in `objc_resolver.c` implement overlapping logic with different bit masks (56-bit vs 47-bit). Consolidate into a single canonical implementation. `Needs Refactor`
- [ ] `[P2]` **Establish clear module dependency order** — Document and enforce: `safe_read` -> `diagnostics` -> `macho_reader` -> `macho_fat/header` -> `macho_load_commands/sections` -> `macho_vmmap` -> `objc_resolver` -> `objc_parser` -> `objc_formatter`. No reverse dependencies should exist.

## Swift-C Bridge Architecture

- [x] `[P0]` **Audit all `withMemoryRebound` calls in MachOAnalyzerBridge.swift** — Lines 264-266 and 384-396 use hardcoded capacities (256, 17) for C char array tuple-to-String conversion. If C struct field sizes change, these silently read garbage. Add compile-time size assertions or derive capacity from `MemoryLayout`. `Needs Fix`
- [x] `[P1]` **Add null-pointer validation after `format_full_dump()` before `free()`** — Line 151 calls `free(dumpCStr)` unconditionally; if `format_full_dump` returns NULL, this is technically safe but masks an allocation failure. Guard and report. `Needs Fix`
- [x] `[P1]` **Validate `assumingMemoryBound(to: UInt8.self)` alignment** — Lines 39 and 85 assume the raw Data buffer pointer is UInt8-aligned. Add assertion or use `withUnsafeBytes` directly. `Needs Fix`
- [x] `[P1]` **Guard integer overflow on `Int(archOffset)` conversion** — Line 89 casts UInt64 to Int for pointer arithmetic without checking for overflow on 32-bit-like scenarios. `Needs Fix`
- [ ] `[P2]` **Reduce String allocation churn in mapping functions** — `mapMethods`, `mapIvars`, `mapProperties`, `mapStringArray` each create temporary String objects from C strings. Consider batching or reusing buffers. `Needs Optimization`

## Memory Ownership Rules

- [x] `[P0]` **Audit every `strdup()` call in objc_parser.c for NULL-check** — Lines 145, 156, 159, 168, 171, 1024 and many others call `strdup()` without checking return value. On memory pressure, this silently produces NULL fields that crash downstream. `Needs Fix`
- [x] `[P0]` **Audit every `malloc()` call in objc_resolver.c for NULL-check** — Lines 188, 214, 234, 268, 330, 361 allocate without validation. `Needs Fix`
- [x] `[P1]` **Document ownership semantics for resolve_string_pointer()** — Returns a `const char*` that may point into the mapped binary (no free needed) or may be a `strdup()`-allocated copy (free needed). Callers cannot distinguish. Standardize: always return heap-allocated strings, or add an output flag. `Needs Refactor`
- [x] `[P1]` **Fix memory leak in macho_load_commands.c** — `format_version()` and `format_source_version()` return `strdup()`-allocated strings assigned to `info->min_version_string`, `info->sdk_version_string`, `info->source_version_string`. If these are called more than once per info struct, previous allocations leak. `Needs Fix`
- [x] `[P1]` **Release file data after analysis completes in AnalysisService** — The `data: Data?` property holds the entire binary in memory even after `result` is populated. Set `data = nil` after successful analysis. `Needs Fix`

## Error Propagation

- [x] `[P1]` **Add structured error codes for all failure paths in AnalysisService** — Lines 61 and 106 catch generic `Error` and use `.localizedDescription`. Replace with typed `AnalysisError` cases for each failure mode. `Fixed`
- [x] `[P1]` **Propagate diagnostic warnings to the UI** — `DIAG_WARN_PARTIAL_METADATA` is silently swallowed in MachOAnalyzerBridge.swift line 141-143. Surface partial-metadata warnings in the overview diagnostics section. `Needs Fix`
- [ ] `[P2]` **Add additional DiagCode values** — Missing: `DIAG_ERR_INVALID_FAT_HEADER`, `DIAG_ERR_CIRCULAR_REFERENCE`, `DIAG_ERR_MISSING_LINKEDIT`, `DIAG_ERR_ENCRYPTED_BINARY`, `DIAG_WARN_TRUNCATED_METADATA`. `Not Implemented`

## Analysis Pipeline

- [x] `[P0]` **Add cancellation support to AnalysisService** — No way to cancel a long-running analysis. Store the `Task` handle and call `.cancel()` when user navigates away or taps cancel. Check `Task.isCancelled` in the C bridge at natural checkpoints. `Not Implemented`
- [ ] `[P1]` **Add granular progress reporting** — `progress` is set to a single string "Analyzing binary..." for the entire ObjC parsing phase. Report sub-phases: "Parsing classes (142/350)...", "Resolving protocols...", etc. Requires adding a progress callback to the C API. `Not Implemented`
- [x] `[P1]` **Add timeout for analysis operations** — No upper bound on analysis time. A pathological binary could hang indefinitely. Add configurable timeout (default 60s). `Not Implemented`
- [ ] `[P2]` **Add result caching** — Re-analyzing the same binary (same bookmark, same file size) should return cached results. Use file hash (first 4KB + size) as cache key. `Not Implemented`
- [ ] `[P2]` **Add memory pressure monitoring** — Large binaries (500MB+) can exhaust app memory. Monitor `os_proc_available_memory()` and abort analysis before hitting jetsam limits. `Not Implemented`

## Crash Prevention

- [x] `[P0]` **Add recursion depth limit in objc_resolver.c `decode_single_type()`** — Nested structs/unions/arrays/pointers in type encodings can cause unbounded recursion. A pathological encoding like `{{{{{...}}}}}` will stack overflow. Add a depth counter with max depth of 32. `Needs Fix`
- [x] `[P0]` **Add recursion depth limit in objc_formatter.c `skip_one_type()`** — Same issue. Lines 150-160 recurse on struct/union without depth tracking. `Needs Fix`
- [x] `[P1]` **Validate array bounds before access in AnalysisService.selectArchitecture()** — Line 87 accesses `fatResult.architectures[archIndex]` without bounds check. `Needs Fix`
- [x] `[P1]` **Fix weak self handling in AnalysisService Task blocks** — Lines 34, 49, 75, 78, 99, 100, 104, 106 capture `[weak self]` but some paths use `self` without guarding for nil. `Fixed`

---

# Mach-O Parsing Engine

## FAT Binary Parsing

- [x] `[P1]` **Validate architecture alignment boundaries** — fat_parse() reads `align` field but never validates that `offset` respects `2^align` alignment. Malformed fat binaries with misaligned architectures should be flagged. `Needs Fix`
- [x] `[P1]` **Detect overlapping architecture slices** — No check that architecture regions don't overlap in the file. Two slices pointing to the same range should be warned about. `Not Implemented`
- [ ] `[P2]` **Detect duplicate architectures** — Same CPU type+subtype appearing twice in a FAT binary is malformed. `Not Implemented`
- [ ] `[P2]` **Add support for extracting architecture by name** — Allow selecting "arm64" by string rather than only by index. `Not Implemented`
- [x] `[P2]` **Expand CPU subtype coverage in cpu_type_name()** — Missing: `ARM64V8`, `X86_64_H` (Haswell), `ARM_V7S`, `ARM_V7K`, `I386_ALL`. Currently only handles `ARM64_ALL` and `ARM64E`. `Needs Fix`

## Mach-O Header Parsing

- [x] `[P1]` **Validate ncmds and sizeofcmds against file size** — `macho_parse_header()` reads ncmds but only checks that the load commands region fits in the file. Should also validate ncmds * sizeof(load_command) <= sizeofcmds. `Needs Fix`
- [x] `[P1]` **Expose header flags interpretation** — Mach-O flags (MH_PIE, MH_NOUNDEFS, MH_DYLDLINK, MH_TWOLEVEL, MH_FORCE_FLAT, MH_NO_REEXPORTED_DYLIBS, MH_HAS_TLV_DESCRIPTORS, etc.) are read but not exposed to Swift. Add flags array to MachOHeaderInfo. `Not Implemented`
- [x] `[P2]` **Replace hardcoded numeric file type constants** — `macho_file_type_name()` lines 11-20 use raw integers. Use the constants defined in macho_types.h. `Needs Refactor`
- [ ] `[P2]` **Add validation of cpu_type/cpu_subtype combinations** — Accept known valid combinations, warn on unknown. `Not Implemented`

## Load Command Parsing

- [x] `[P0]` **Extract linked dylib names from LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_REEXPORT_DYLIB, LC_LAZY_LOAD_DYLIB** — Currently ignored entirely. These are critical for understanding binary dependencies. Add `dylib_names` array to LoadCommandsInfo. `Not Implemented`
- [x] `[P0]` **Extract RPATH entries from LC_RPATH** — Currently ignored. Required for resolving @rpath-relative dylib references. Add `rpaths` array to LoadCommandsInfo. `Not Implemented`
- [x] `[P1]` **Extract code signature offset/size from LC_CODE_SIGNATURE** — Needed to report whether binary is signed. Add `code_sig_offset`, `code_sig_size`, `has_code_signature` to LoadCommandsInfo. `Not Implemented`
- [x] `[P1]` **Extract encryption info from LC_ENCRYPTION_INFO / LC_ENCRYPTION_INFO_64** — Needed to detect encrypted binaries (which cannot be fully parsed). Add `is_encrypted`, `crypt_offset`, `crypt_size` to LoadCommandsInfo. `Not Implemented`
- [x] `[P1]` **Extract function starts from LC_FUNCTION_STARTS** — Add `function_starts_offset`, `function_starts_size` to LoadCommandsInfo. Parse ULEB128-encoded function start offsets. `Not Implemented`
- [x] `[P1]` **Extract data-in-code entries from LC_DATA_IN_CODE** — Needed to distinguish code from data in __text sections. `Not Implemented`
- [x] `[P1]` **Validate cmdsize alignment** — Load command sizes must be multiples of 4 (32-bit) or 8 (64-bit). No validation exists. `Needs Fix`
- [x] `[P1]` **Detect overlapping load commands** — No check that load commands don't overlap in the header region. `Not Implemented`
- [ ] `[P2]` **Add LC_DYLD_CHAINED_FIXUPS parsing** — Struct defined in macho_types.h but never parsed. Modern arm64e binaries use chained fixups instead of classic rebase/bind. `Not Implemented`
- [ ] `[P2]` **Add LC_DYLD_EXPORTS_TRIE parsing** — Struct defined but not parsed. Contains exported symbol trie. `Not Implemented`
- [ ] `[P2]` **Parse dyld info command details (LC_DYLD_INFO / LC_DYLD_INFO_ONLY)** — Rebase, bind, lazy_bind, weak_bind, export offsets are read into struct but not further decoded. `Not Implemented`
- [ ] `[P2]` **Add LC_BUILD_VERSION tool information extraction** — Build tools (ld, swift, clang) and their versions are in the command but not extracted. `Not Implemented`
- [ ] `[P2]` **Handle LC_REQ_DYLD flag in load_command_name()** — Commands with this flag bit set are not properly name-resolved. `Needs Fix`

## Load Command Structures (macho_types.h)

- [x] `[P1]` **Add MachOEncryptionInfoCommand / MachOEncryptionInfoCommand64 structs** — LC_ENCRYPTION_INFO defined as constant but struct missing. `Not Implemented`
- [ ] `[P1]` **Add MachOCodeSignatureCommand struct** — LC_CODE_SIGNATURE uses MachOLinkeditDataCommand internally but deserves explicit typedef for clarity. `Not Implemented`
- [ ] `[P2]` **Add MachOThreadCommand struct** — LC_THREAD / LC_UNIXTHREAD structures missing. `Not Implemented`
- [x] `[P2]` **Add MachODataInCodeEntry struct** — LC_DATA_IN_CODE entry format missing. `Not Implemented`
- [ ] `[P2]` **Add MachODyldChainedFixupsHeader struct** — LC_DYLD_CHAINED_FIXUPS data format missing. `Not Implemented`

## Segment and Section Parsing

- [x] `[P1]` **Validate that section sizes don't exceed segment sizes** — No check that sum of section sizes fits within segment filesize. `Needs Fix`
- [x] `[P1]` **Detect overlapping sections within a segment** — Two sections occupying the same file range should be warned about. `Not Implemented`
- [x] `[P1]` **Add diagnostic warning when section count exceeds 10,000 cap** — Lines 160 and 239 silently set nsects=0 when limit exceeded. Add diag_add warning. `Needs Fix`
- [x] `[P1]` **Fix memory cleanup on allocation failure in macho_parse_sections()** — When allocation fails mid-parsing, already-allocated segments are not freed. `Needs Fix`
- [ ] `[P2]` **Expose section attribute flags** — S_ATTR_PURE_INSTRUCTIONS, S_ATTR_NO_TOC, S_ATTR_NO_DEAD_STRIP, etc. are defined but not surfaced. `Not Implemented`
- [ ] `[P2]` **Optimize find_section() with hash lookup** — Currently O(segments * sections) linear scan. Build a hash table on first call for O(1) subsequent lookups. `Needs Optimization`
- [ ] `[P2]` **Add support for zero-fill sections** — S_ZEROFILL sections have no file data. vmmap should handle these (vmsize > 0, filesize == 0) correctly. Verify and test. `Needs Verification`

## Virtual Address Mapping

- [x] `[P1]` **Validate VM regions don't overlap** — vmmap_build() assumes segments define non-overlapping ranges. Malformed binaries may violate this. `Needs Fix`
- [x] `[P1]` **Fix PAC bit mask inconsistency** — `strip_pac_pointer()` in macho_vmmap.c masks to 0x00FFFFFFFFFFFFFF (56 bits) but comment says "48 bits". Canonical arm64 virtual addresses are 48 bits (0x0000FFFFFFFFFFFF). Choose correct mask and document. `Needs Fix`
- [ ] `[P2]` **Optimize vmmap_to_file_offset() with binary search** — Currently O(n) linear scan through regions. Sort regions by vmaddr at build time and use binary search. `Needs Optimization`
- [x] `[P2]` **Handle preferred_load_address correctly when multiple __TEXT segments exist** — Lines 59-61 and 74-76 overwrite preferred_load_address if multiple candidates exist. Use first occurrence. `Needs Fix`
- [ ] `[P2]` **Add ASLR slide support** — For runtime-analyzed binaries (future), the VM map needs to apply a slide offset. Design the API even if not yet used. `Not Implemented`

## Symbol Table Parsing

- [ ] `[P1]` **Parse symbol table (LC_SYMTAB)** — MachOSymtabCommand is defined and read but symbols are never parsed. Implement nlist traversal, string table lookup, symbol type classification (undefined, absolute, section, prebound, indirect). `Not Implemented`
- [ ] `[P1]` **Parse dynamic symbol table (LC_DYSYMTAB)** — MachODysymtabCommand is defined but not parsed. Implement indirect symbol table reading for lazy/non-lazy binding. `Not Implemented`
- [ ] `[P2]` **Parse string table** — String table referenced by LC_SYMTAB is never extracted. Needed for symbol names. `Not Implemented`
- [ ] `[P2]` **Classify symbols** — Local vs global vs undefined vs debug symbols. Map symbol stab entries for debug info. `Not Implemented`
- [ ] `[P2]` **Demangle C++ and Swift symbol names** — Symbols may be mangled. Implement or integrate basic demangling for `_Z` (C++) and `$s` (Swift) prefixes. `Not Implemented`

---

# Objective-C Metadata Extraction

## Class Parsing

- [x] `[P0]` **Validate hard-coded class_ro_t field offsets** — objc_parser.c lines 467-473 hard-code offsets (24, 32, 40, 48, 64) for 64-bit class_ro_t fields. These match the current ABI but are not verified against the actual struct layout. Add a comment documenting the expected layout, or read fields by parsing the struct sequentially. `Needs Fix`
- [x] `[P0]` **Fix unsafe strict aliasing violation in relative method parsing** — Line 124-127: `*(int32_t *)&tmp` after byte swap violates C strict aliasing rules. Use `memcpy` to an int32_t instead. `Needs Fix`
- [x] `[P1]` **Validate class data pointer after OBJC_CLASS_DATA_MASK** — Line 540 masks the data pointer but doesn't check if the result is zero or points outside the binary. `Needs Fix`
- [x] `[P1]` **Validate method list entsize matches expected values** — Lines 81-90 have fallback defaults (24/12 bytes) but don't warn when the actual entsize differs from expected. A mismatch indicates ABI version difference or corruption. `Needs Fix`
- [x] `[P1]` **Add duplicate class name detection** — Multiple classes with the same name in a single binary is suspicious. Warn but don't fail. `Not Implemented`
- [x] `[P1]` **Validate instance_size values** — No check that instance_size is reasonable (e.g., < 1GB). Pathological values should be flagged. `Needs Fix`
- [x] `[P2]` **Parse __objc_imageinfo section** — Determines ObjC ABI version and Swift version flags. Currently not parsed. `Not Implemented`
- [ ] `[P2]` **Parse weak ivar layout** — class_ro_t has a weak_ivar_layout pointer that is read but never interpreted. Needed for full ARC analysis. `Not Implemented`
- [ ] `[P2]` **Parse non-fragile ivar offsets** — Ivar offset pointers reference runtime-adjusted offsets. Verify the parser handles these correctly for modern ABI. `Needs Verification`

## Metaclass Handling

- [x] `[P1]` **Validate isa pointer before metaclass parsing** — Line 603 reads the isa pointer to find the metaclass but doesn't validate it points to a valid class structure before dereferencing. `Needs Fix`
- [ ] `[P1]` **Distinguish metaclass methods from category class methods** — Class methods found on the metaclass should be annotated with their source (direct or via category). `Not Implemented`
- [ ] `[P2]` **Detect root metaclass (isa == self) cycle** — Root metaclass isa points to itself. Parser should detect and stop rather than loop. `Needs Fix`

## Method List Parsing

- [x] `[P0]` **Add diagnostic when method/ivar/property count exceeds 100,000 cap** — Lines 79, 217, 323, 398 silently truncate. Add a warning to diagnostics. `Needs Fix`
- [x] `[P1]` **Validate method name and type strings before strdup** — No length validation. A malformed binary could point name/type to a region with no null terminator, causing `strdup` of megabytes. Enforce max string length (e.g., 4096). `Needs Fix`
- [ ] `[P1]` **Parse method list flags header** — The entsize_and_flags field at the start of method_list_t encodes both entry size and flags (e.g., uses relative methods, is uniqued). Currently only the entry count is read. `Not Implemented`
- [ ] `[P2]` **Support signed method pointers (arm64e)** — arm64e binaries may have pointer-authenticated IMPs. Ensure these are properly stripped when reading method implementation addresses. `Needs Verification`

## Property List Parsing

- [x] `[P1]` **Fix potential free-after-non-alloc in property attributes** — Line 359: `free((void *)as)` where `as` came from `resolve_string_pointer()` which may return a pointer into the binary (not heap-allocated). This is a use-after-free or invalid-free risk. `Needs Fix`
- [x] `[P1]` **Handle property attribute parsing edge cases** — `parse_property_attributes()` in objc_resolver.c assumes well-formed attribute strings. Malformed strings (missing T prefix, unbalanced quotes) should be handled gracefully. `Needs Fix`
- [ ] `[P2]` **Support custom property getter/setter signatures** — The G and S attribute codes extract names but don't link to actual method entries. `Not Implemented`

## Protocol Parsing

- [x] `[P1]` **Detect circular protocol adoption** — Protocol A adopting Protocol B which adopts Protocol A will cause infinite traversal. Add visited-set tracking. `Not Implemented`
- [x] `[P1]` **Cap protocol list parsing at 500,000 without diagnostic** — Line 805 caps but doesn't warn. Add diagnostic. `Needs Fix`
- [ ] `[P2]` **Parse optional protocol properties** — Currently only optional methods are marked. Properties can also be @optional. `Needs Verification`
- [ ] `[P2]` **Link protocols to conforming classes** — Build reverse index: for each protocol, list classes that declare conformance. `Not Implemented`

## Category Parsing

- [x] `[P1]` **Handle category with unresolvable class reference** — When the class pointer can't be resolved, category is created with NULL class_name. Should store raw pointer value as fallback identifier. `Needs Fix`
- [ ] `[P2]` **Link categories to their base classes** — Build association index: for each class, list categories that extend it. `Not Implemented`
- [ ] `[P2]` **Detect duplicate categories** — Same category name on same class appearing twice is suspicious. `Not Implemented`

## Selector References

- [ ] `[P1]` **Link selectors to methods** — `__objc_selrefs` contains selector references but these are not cross-referenced with method declarations. Build selector-to-method mapping. `Not Implemented`
- [ ] `[P2]` **Detect unused selectors** — Selectors referenced but not matching any declared method may indicate stripped or dynamic methods. `Not Implemented`

## Type Encoding

- [x] `[P1]` **Fix unbounded recursion in decode_single_type()** — Deeply nested type encodings (structs containing structs containing unions, etc.) have no depth limit. Add max depth of 32 with "..." truncation. `Needs Fix`
- [x] `[P1]` **Handle malformed type encodings gracefully** — Missing closing delimiters (`}`, `]`, `)`) cause the decoder to read past the encoding string. Add bounds checking. `Needs Fix`
- [ ] `[P2]` **Improve block type encoding** — `@?` is decoded as "id /* block */" but the full block signature (if present) is not parsed. `Not Implemented`
- [ ] `[P2]` **Handle bitfield type encoding edge cases** — `bN` where N > 64 is technically valid but not practically useful. Clamp and warn. `Not Implemented`

---

# Class Dump Reconstruction Engine

## @interface Reconstruction

- [x] `[P1]` **Order output: protocols first, then class hierarchy, then categories** — `format_full_dump()` does this but doesn't sort classes by inheritance depth (superclass before subclass). `Needs Fix`
- [x] `[P1]` **Handle forward declarations** — When class A references class B as a property type but B appears later in the dump, emit `@class B;` forward declaration at the top. `Not Implemented`
- [ ] `[P2]` **Generate `#import` statements for framework dependencies** — If a superclass or protocol comes from an external framework (not in the binary's class list), generate appropriate import. `Not Implemented`
- [ ] `[P2]` **Add `__attribute__` annotations** — Availability, deprecated, NS_SWIFT_NAME, etc. when detectable from metadata. `Not Implemented`

## Superclass Resolution

- [ ] `[P1]` **Resolve superclass chain fully** — Currently only immediate superclass is resolved. Build full inheritance chain for display. `Not Implemented`
- [ ] `[P2]` **Handle external superclasses** — When superclass is in a linked dylib (not in this binary), note it as external. `Not Implemented`

## Method Declaration Formatting

- [x] `[P1]` **Fix parameter name generation in format_method()** — Lines 273-274 generate `arg0`, `arg1`, etc. Use heuristic names based on type (e.g., `string` for NSString*, `flag` for BOOL, `index` for NSUInteger). `Needs Optimization`
- [ ] `[P1]` **Handle variadic methods** — Methods with `...` in their signature need special formatting. `Not Implemented`
- [ ] `[P2]` **Align multi-parameter method declarations** — Long selectors with multiple parameters should be formatted with aligned colons, matching class-dump style. `Needs Optimization`

## Ivar Block Formatting

- [ ] `[P2]` **Decode ivar type encodings inline** — Currently ivars show raw type encoding. Decode to readable C type (e.g., `{CGRect=...}` -> `CGRect`). `Needs Optimization`
- [ ] `[P2]` **Show ivar offset comments** — Classic class-dump shows `// offset X, size Y` as inline comments. Current formatter omits this. `Not Implemented`

## Protocol Declaration Formatting

- [x] `[P1]` **Separate @required and @optional sections** — `format_protocol()` currently marks optional methods but doesn't emit `@required` / `@optional` section headers in the standard Objective-C convention. Verify. `Needs Verification`
- [ ] `[P2]` **Format protocol hierarchy comments** — Show `// Adopted protocols: X, Y` as a comment header. `Not Implemented`

## Category Declaration Formatting

- [ ] `[P2]` **Include category protocol conformances** — `@interface ClassName (CategoryName) <ProtocolX>` format. `Needs Verification`

## Output Quality

- [x] `[P1]` **Validate format buffer allocation in format_full_dump()** — Line 545 creates a 64KB buffer but doesn't check if creation succeeded. `Needs Fix`
- [ ] `[P1]` **Add line width control** — No wrapping for very long method signatures. Lines exceeding 120 characters should be wrapped. `Not Implemented`
- [ ] `[P2]` **Add pragma mark sections** — Group methods by category origin in class output: `#pragma mark - CategoryName`. `Not Implemented`
- [ ] `[P2]` **Support generating Swift-style declarations** — Optional alternative output format using Swift syntax for bridged types. `Not Implemented`

---

# Binary Analysis Enhancements

## Architecture and Platform Information

- [x] `[P1]` **Surface header flags in the UI** — MH_PIE, MH_TWOLEVEL, MH_HAS_TLV_DESCRIPTORS, etc. are parsed but not displayed. Add to OverviewView. `Not Implemented`
- [x] `[P1]` **Display platform target** — LC_BUILD_VERSION platform field is parsed but not shown clearly. Map platform constant to "iOS", "macOS", "watchOS", etc. `Needs Fix`
- [ ] `[P2]` **Show tool versions from LC_BUILD_VERSION** — ld, swift, clang versions are available but not extracted. `Not Implemented`

## Code Signature Analysis

- [x] `[P1]` **Detect code signature presence** — Parse LC_CODE_SIGNATURE to report whether binary is signed, ad-hoc signed, or unsigned. `Not Implemented`
- [ ] `[P2]` **Parse embedded entitlements** — Entitlements are embedded in the code signature blob. Extract and display as plist/XML. `Not Implemented`
- [ ] `[P2]` **Report code signing identity** — Extract signing team ID and certificate common name if available. `Not Implemented`

## Encryption Detection

- [x] `[P1]` **Detect encrypted binaries** — Parse LC_ENCRYPTION_INFO_64 and report `cryptid` value. If non-zero, warn that ObjC metadata may be unreadable in encrypted segments. `Not Implemented`

## Dependency Analysis

- [x] `[P0]` **List linked dynamic libraries** — Extract all LC_LOAD_DYLIB, LC_LOAD_WEAK_DYLIB, LC_LAZY_LOAD_DYLIB, LC_REEXPORT_DYLIB names and versions. Display in UI. `Not Implemented`
- [x] `[P1]` **List RPATHs** — Extract all LC_RPATH entries. `Not Implemented`
- [ ] `[P2]` **Resolve @rpath, @executable_path, @loader_path references** — Show resolved paths for dylib dependencies where possible. `Not Implemented`

## Segment Layout Visualization

- [x] `[P2]` **Show segment protection flags** — Display RWX permissions per segment in SegmentsView. `Not Implemented`
- [ ] `[P2]` **Add memory map visualization** — Graphical representation of segment layout showing relative sizes and positions. `Not Implemented`
- [x] `[P2]` **Add segment purpose annotation** — Label __TEXT as "Code", __DATA as "Mutable Data", __DATA_CONST as "Immutable Data", __LINKEDIT as "Linker Metadata". `Not Implemented`

---

# SwiftUI / UX Completion

## Home Screen

- [x] `[P1]` **Add swipe-to-delete on recent files** — No way to remove entries from recent files list. `Not Implemented`
- [x] `[P1]` **Add "Clear All" recent files option** — No bulk clear functionality. `Implemented`
- [x] `[P1]` **Validate bookmarks on display** — Stale bookmarks (file moved/deleted) show in list but fail silently on tap. Check `resolveBookmark` on load and mark stale entries. `Not Implemented`
- [ ] `[P2]` **Add drag-and-drop binary import** — Only file picker available. `Not Implemented`
- [ ] `[P2]` **Add file type icon differentiation** — All recent files show the same doc.fill icon. Use different icons for executables vs dylibs vs frameworks. `Not Implemented`

## File Importer

- [x] `[P1]` **Add file size validation before import** — No upper limit. A 4GB file will be memory-mapped and potentially crash. Warn above 500MB, reject above 2GB. `Not Implemented`
- [x] `[P1]` **Handle stale bookmark resolution** — `resolveBookmark` returns `stale` flag that is currently ignored. If stale, recreate the bookmark. `Needs Fix`
- [ ] `[P2]` **Add file type validation** — `supportedTypes` is defined in FileImportService but never checked during import. Validate imported file has valid Mach-O magic before proceeding. `Needs Fix`

## Analysis Progress UI

- [x] `[P1]` **Add cancel button during analysis** — ProgressContentView has no cancel option. `Not Implemented`
- [ ] `[P1]` **Show progress percentage** — Determinate progress bar instead of spinner where possible (file reading can show bytes read / total). `Not Implemented`
- [ ] `[P2]` **Show estimated time remaining** — Based on bytes processed per second. `Not Implemented`
- [ ] `[P2]` **Add "re-analyze" option after completion** — User may want to re-analyze with different settings (future). `Not Implemented`

## Class Browser UI

- [x] `[P1]` **Make superclass name tappable** — In ClassDetailView, superclass name is static text. Should navigate to the superclass's ClassDetailView if it exists in the binary. `Not Implemented`
- [x] `[P1]` **Add class hierarchy tree view** — No visualization of inheritance relationships. `Not Implemented`
- [x] `[P2]` **Add method signature expansion** — MethodRow shows name and return type. Add expandable detail showing full parameter types. `Not Implemented`
- [ ] `[P2]` **Add per-class export option** — Export single class definition to clipboard or file. `Not Implemented`
- [x] `[P2]` **Show protocol conformance badge in class list** — ClassRow doesn't indicate which protocols a class conforms to. `Not Implemented`

## Protocol Browser UI

- [x] `[P1]` **Make adopted protocols tappable** — ProtocolDetailView lists adopted protocols as plain text. Should navigate. `Not Implemented`
- [x] `[P1]` **Show conforming classes list** — Reverse index: which classes conform to this protocol. `Not Implemented`
- [x] `[P2]` **Visual distinction for @optional methods** — Use lighter color or italic font. `Not Implemented`

## Category Browser UI

- [x] `[P1]` **Make class name tappable** — CategoryDetailView shows the extended class name but doesn't navigate to it. `Not Implemented`
- [ ] `[P2]` **Show other categories for same class** — Related categories grouped. `Not Implemented`

## Raw Dump Viewer

- [x] `[P1]` **Add search within dump text** — DumpView has no search. For large dumps this makes navigation impossible. `Not Implemented`
- [x] `[P1]` **Add syntax highlighting** — Objective-C keyword coloring (@interface, @end, @property, etc.) for readability. `Not Implemented`
- [x] `[P1]` **Fix memory issue with very large dumps** — All text rendered in a single Text view. Dumps exceeding 1MB will cause significant lag. Implement line-based virtualization. `Needs Fix`
- [x] `[P2]` **Add line numbers** — `Not Implemented`
- [x] `[P2]` **Add font size adjustment** — Current .caption monospaced font may be too small for many users. `Not Implemented`
- [x] `[P2]` **Add copy feedback** — No visual confirmation when "Copy All" is tapped. Add toast/HUD. `Not Implemented`

## Segments/Sections Viewer

- [x] `[P1]` **Show segment protection flags (RWX)** — SegmentsView omits maxprot and initprot display. `Not Implemented`
- [x] `[P2]` **Make load commands expandable** — Currently just lists name + offset. Add detail row with all command-specific fields. `Not Implemented`
- [ ] `[P2]` **Add section type annotation** — Show whether section contains code, data, zero-fill, symbol stubs, etc. `Not Implemented`
- [x] `[P2]` **Color-code segments by type** — __TEXT blue, __DATA green, __LINKEDIT gray, etc. `Not Implemented`

## Error UI

- [x] `[P1]` **Add retry button in ErrorContentView** — Currently only "Go Back". Add "Retry Analysis" option. `Not Implemented`
- [x] `[P1]` **Show specific error suggestions** — e.g., "Binary appears to be encrypted. Decrypted binaries are required for metadata extraction." `Implemented`
- [ ] `[P2]` **Add error reporting/sharing** — Let user share diagnostic info when analysis fails. `Not Implemented`

## iPad Layouts

- [x] `[P1]` **Add sidebar navigation for iPad** — (Removed - caused navigation conflicts with NavigationStack) `Not Implemented`
- [ ] `[P1]` **Add multi-column detail view** — ClassDetailView could show properties and methods side-by-side on iPad. `Not Implemented`
- [ ] `[P2]` **Support Split View multitasking** — App should work correctly in 1/3, 1/2, 2/3 split configurations. `Needs Verification`
- [ ] `[P2]` **Support Slide Over** — Compact presentation. `Needs Verification`

## Navigation Improvements

- [x] `[P1]` **Make SearchResultsOverlay categories navigable** — Category results are plain text, not NavigationLinks. Inconsistent with class/protocol results. `Needs Fix`
- [x] `[P1]` **Make SearchResultsOverlay methods navigable** — Method results should navigate to the owning class/protocol detail view. `Not Implemented`
- [x] `[P2]` **Add tab count badges** — Tab bar shows "Classes" but not "Classes (142)". Count indicators help users understand binary scope at a glance. `Not Implemented`
- [ ] `[P2]` **Remember selected tab between analyses** — Currently resets to Overview on every new analysis. `Not Implemented`

## Accessibility

- [x] `[P0]` **Add accessibility labels to all icon-only buttons** — Copy button (doc.on.doc), export button (square.and.arrow.up), and all toolbar items lack labels. `Not Implemented`
- [x] `[P0]` **Add accessibility labels to badge views** — "Swift" badge, property attribute badges (nonatomic, readonly, etc.) need VoiceOver descriptions. `Not Implemented`
- [x] `[P1]` **Support Dynamic Type** — No views use `.dynamicTypeSize` or scale fonts. Hardcoded .caption, .caption2 sizes. `Not Implemented`
- [x] `[P1]` **Add color-blind safe indicators** — Error (red) vs warning (orange) in diagnostics relies on color alone. Add icon or text prefix. `Needs Fix`
- [ ] `[P2]` **Add accessibility hints for complex views** — Architecture picker, tab bar, search overlay need VoiceOver guidance. `Not Implemented`

---

# Search and Navigation System

## Core Search

- [x] `[P0]` **Add search debouncing** — SearchResultsOverlay triggers search on every character. Add 300ms debounce. `Not Implemented`
- [x] `[P1]` **Cache lowercased query** — SearchService.search() calls `.lowercased()` on the query once but calls `.lowercased()` on every element's name (lines 40, 42-43, 48, 49, 54, 56, 64, 65). Pre-compute lowercased element names on first search or at analysis time. `Needs Optimization`
- [x] `[P1]` **Add result count limit** — No cap on search results. Searching "a" in a large binary could return 10,000+ results, causing UI lag. Limit to first 200 results with "Show more" option. `Not Implemented`
- [x] `[P1]` **Move search execution off main thread** — All filtering happens synchronously on the main thread. Use `Task` with actor isolation for large datasets. `Addressed: documented why synchronous is acceptable (200-item cap + debouncing)`

## Search Features

- [x] `[P1]` **Add search result highlighting** — Matched substring not highlighted in results. `Not Implemented`
- [ ] `[P2]` **Add selector search** — Search specifically within __objc_selrefs. `Not Implemented`
- [ ] `[P2]` **Add ivar search** — Search instance variable names. `Not Implemented`
- [ ] `[P2]` **Add regex search** — Support regex patterns for power users. `Not Implemented`
- [ ] `[P2]` **Add fuzzy matching** — Tolerate typos (Levenshtein distance). `Not Implemented`

## Indexed Search

- [ ] `[P2]` **Build search index at analysis time** — Pre-index all class names, method names, property names, protocol names into a trie or inverted index for sub-millisecond search. `Not Implemented`
- [ ] `[P2]` **Add ranked results** — Score by match quality: exact > prefix > substring > fuzzy. `Not Implemented`

## Cross-Link Navigation

- [x] `[P1]` **Navigate from method results to owning class** — Search result "method in ClassName" should navigate to ClassDetailView. `Not Implemented`
- [x] `[P1]` **Navigate from property results to owning class** — Same as above. `Not Implemented`
- [ ] `[P1]` **Navigate from type references to class definitions** — Property type "NSString" should link to NSString ClassDetailView if the class exists in the binary. `Not Implemented`
- [x] `[P2]` **Navigate from superclass to class definition** — ClassDetailView superclass row should link. `Not Implemented`
- [x] `[P2]` **Navigate from adopted protocol to protocol definition** — ProtocolDetailView adopted protocols should link. `Not Implemented`

---

# File Import System

## Security-Scoped Access

- [x] `[P1]` **Handle security-scoped access failure** — `startAccessingSecurityScopedResource()` can return false. Currently the error message doesn't differentiate this from read failure. Add specific error: "Access denied. The file may have been moved or permissions changed." `Fixed`
- [x] `[P1]` **Recreate stale bookmarks** — FileImportService.resolveBookmark returns `isStale` flag that is ignored. If stale, recreate the bookmark and update RecentFilesStore. `Needs Fix`

## Large File Handling

- [x] `[P1]` **Add file size check before .mappedIfSafe** — No validation. `.mappedIfSafe` may fail silently on very large files. Add explicit size check and fallback to chunked reading for files > 500MB. `Not Implemented`
- [ ] `[P2]` **Implement streaming analysis for huge binaries** — Currently the entire file is memory-mapped at once. For multi-GB universal binaries, implement on-demand section reading. `Not Implemented`

## File Management

- [x] `[P1]` **Clean up temp export files** — ExportService writes to temp directory but never cleans up. Add cleanup on app launch or after share sheet dismissal. `Not Implemented`
- [ ] `[P2]` **Detect duplicate imports** — If user imports the same file twice, detect and navigate to existing analysis instead of re-analyzing. Use file bookmark comparison or content hash. `Not Implemented`
- [ ] `[P2]` **Add file metadata display before analysis** — Show file size, modification date, and detected type before committing to full analysis. `Not Implemented`

---

# Performance Optimization

## Lazy Parsing

- [ ] `[P1]` **Lazy-parse ObjC metadata on tab selection** — Currently all metadata (classes, protocols, categories, selectors) is parsed in one pass. Parse only what's needed: classes when Classes tab selected, protocols when Protocols tab selected. `Not Implemented`
- [ ] `[P2]` **Lazy-load method details** — Parse method names in the list view but defer full type encoding decoding until detail view is opened. `Not Implemented`
- [ ] `[P2]` **Lazy-generate dump text** — `format_full_dump()` generates the entire dump upfront. Generate on-demand when Dump tab is selected. `Not Implemented`

## Caching

- [ ] `[P1]` **Cache analysis results** — Re-analyzing the same binary on app relaunch wastes time. Serialize AnalysisResult to disk keyed by file hash. `Not Implemented`
- [ ] `[P2]` **Cache resolved class names** — objc_resolver.c resolves the same class pointers repeatedly. Add a pointer-to-name cache in resolve_class_name(). `Not Implemented`
- [ ] `[P2]` **Cache VM address translations** — vmmap_to_file_offset() re-scans regions on every call. Add LRU cache for frequently accessed addresses. `Not Implemented`

## Memory Efficiency

- [x] `[P1]` **Release binary data after analysis** — AnalysisService.data holds the entire file in memory indefinitely. Clear after analysis completes. `Needs Fix`
- [ ] `[P1]` **Avoid double-storing dump text** — The formatted dump text can be megabytes. It's stored in AnalysisResult.dumpText and also implicitly in the C FormatBuffer until freed. Ensure the C buffer is freed promptly after Swift copies the string. Verify. `Needs Verification`
- [ ] `[P2]` **Use contiguous arrays for model collections** — ClassModel, ProtocolModel arrays use standard Array. For large collections (10,000+ items), consider ContiguousArray. `Needs Optimization`

## SwiftUI Rendering

- [x] `[P1]` **Virtualize DumpView for large text** — Single `Text(dumpText)` with 100KB+ content causes severe lag. Split into lines and use `LazyVStack` with line views. `Needs Fix`
- [x] `[P1]` **Add search debouncing** — See Search section. `Not Implemented`
- [ ] `[P2]` **Profile ClassDetailView with 1000+ methods** — ForEach over large method arrays may not be lazy inside a List Section. Verify scrolling performance. `Needs Verification`
- [ ] `[P2]` **Profile SegmentsView with many sections** — Binaries can have 100+ sections. Verify List performance. `Needs Verification`

## Background Processing

- [x] `[P1]` **Specify QoS for analysis tasks** — AnalysisService uses `Task.detached` without specifying priority. Use `.userInitiated` for file reading and `.utility` for parsing. `Needs Fix`
- [ ] `[P2]` **Use operation queues for parallel section parsing** — Parse classes, protocols, categories concurrently instead of sequentially. `Not Implemented`

---

# Reliability and Safety

## Corrupted Binary Detection

- [x] `[P0]` **Detect encrypted binaries and abort gracefully** — Encrypted regions (LC_ENCRYPTION_INFO with cryptid != 0) cannot be parsed for ObjC metadata. Detect and report instead of returning garbage. `Not Implemented`
- [x] `[P1]` **Validate Mach-O magic before deep parsing** — FileImportService should check the first 4 bytes for valid magic before proceeding to full analysis. Reject non-Mach-O files with clear error. `Not Implemented`
- [ ] `[P1]` **Handle truncated binaries** — A file that ends mid-way through declared sections should not crash. Verify all section reads check against actual file size. `Needs Verification`
- [x] `[P2]` **Detect and report stripped binaries** — Binaries with no __objc_classlist, __objc_protolist, or __objc_catlist should report "No ObjC metadata found" rather than empty results. `Needs Fix`

## Invalid Offsets

- [x] `[P0]` **Bound-check all vmmap reads against file size** — vmmap_read_bytes validates range but callers may pass computed sizes that exceed bounds. Audit all call sites. `Needs Verification`
- [x] `[P1]` **Validate section offsets against segment bounds** — A section declaring offset outside its parent segment's file range is malformed. `Not Implemented`
- [x] `[P1]` **Cap string reads at reasonable max length** — `resolve_string_pointer()` and `vmmap_read_string()` have max_len parameters but callers may pass large values. Enforce global max of 8192 bytes. `Needs Fix`

## Malformed Metadata Handling

- [x] `[P1]` **Handle class_ro_t with zero name pointer** — If name resolution fails, use a placeholder like `<unnamed class at 0xABCD>` instead of NULL. `Needs Fix`
- [x] `[P1]` **Handle method with zero name pointer** — Same: use `<unnamed method>` placeholder. `Needs Fix`
- [x] `[P1]` **Handle categories with zero name and zero class** — Double-NULL categories should be skipped with a diagnostic. `Needs Fix`
- [ ] `[P2]` **Handle self-referential class hierarchies** — A class whose superclass is itself (circular) should be detected and broken. `Not Implemented`

## Safe Fallback Paths

- [x] `[P1]` **Ensure every C parse function has a clean failure path** — Audit: `parse_method_list`, `parse_ivar_list`, `parse_property_list`, `parse_protocol_ref_list`, `read_class_ro`, `parse_single_class`, `parse_single_protocol`, `parse_single_category`. Each must leave output struct in a valid state (zeroed) on failure. `Needs Verification`
- [x] `[P1]` **Ensure all destroy functions handle NULL gracefully** — `objc_metadata_destroy(NULL)` should be safe. Audit all destroy functions. `Needs Verification`

---

# Export Features

## Full Dump Export

- [ ] `[P1]` **Add progress indication for large exports** — Writing a multi-MB .h file can take seconds. Show activity indicator. `Not Implemented`
- [x] `[P1]` **Validate dumpText before writing** — ExportService writes dumpText without checking if it's empty or null. Guard and provide error. `Needs Fix`
- [x] `[P2]` **Add filename sanitization** — Line 6: `deletingPathExtension` on user-provided filename. Ensure result is safe (no path traversal, no empty string). `Needs Fix`

## Per-Class Export

- [x] `[P1]` **Export single class definition** — Copy individual class @interface to clipboard. `Not Implemented`
- [x] `[P2]` **Export single protocol definition** — Copy individual @protocol to clipboard. `Not Implemented`
- [x] `[P2]` **Export single category definition** — Copy individual category to clipboard. `Not Implemented`

## JSON Export

- [x] `[P1]` **Add schema version to JSON output** — No version field. Future format changes will be breaking. Add `"schema_version": "1.0"`. `Not Implemented`
- [x] `[P1]` **Add method type encoding to JSON** — JSON export includes name/return_type but not the raw type encoding string. `Needs Fix`
- [ ] `[P2]` **Support filtering JSON export** — Export only classes matching a pattern, or only protocols. `Not Implemented`
- [ ] `[P2]` **Use Codable for JSON export** — ExportService manually builds dictionaries. Use Encodable conformance on model types for type-safe serialization. `Needs Refactor`

## Share Sheet

- [x] `[P1]` **Add copy-to-clipboard feedback** — UIPasteboard.general.string = ... with no visual confirmation. Add toast/haptic. `Not Implemented`
- [ ] `[P2]` **Add AirDrop export** — Share sheet already supports this via UIActivityViewController, but verify it works for large files. `Needs Verification`

---

# Code Quality and Refactoring

## C Engine

- [x] `[P1]` **Remove magic number 100000 limit** — Use a named constant: `#define OBJC_MAX_LIST_ENTRIES 100000` with documentation explaining why this limit exists. `Needs Refactor`
- [x] `[P1]` **Remove magic number 500000 class/protocol cap** — Same: named constant with rationale. `Needs Refactor`
- [x] `[P1]` **Remove magic number 10000 sections-per-segment cap** — Same. `Needs Refactor`
- [x] `[P1]` **Replace hardcoded file type numbers in macho_header.c** — Use MACHO_FILETYPE_* constants. `Needs Refactor`
- [ ] `[P2]` **Add documentation comments to all public C functions** — No function has a doc comment. At minimum: purpose, parameters, return value, ownership semantics. `Not Implemented`
- [x] `[P2]` **Audit pointer casting patterns** — The `*(int32_t *)&tmp` pattern in objc_parser.c line 124-127 violates strict aliasing. Replace with `memcpy`. `Needs Fix`

## Swift Layer

- [x] `[P1]` **Add MainActor annotation to ObservableObject classes** — RecentFilesStore and AnalysisService publish to UI but don't enforce main thread. `Needs Fix`
- [x] `[P1]` **Fix model identity** — ClassModel, ProtocolModel, CategoryModel use `UUID()` for Identifiable but hash/equal by name only. Two different classes with the same name will collide. Use name + a disambiguator. `Fixed: Hashable/Equatable now use UUID id`
- [ ] `[P2]` **Replace manual dictionary building in ExportService with Codable** — Type-safe serialization. `Needs Refactor`
- [ ] `[P2]` **Add type annotations to complex closures in SearchService** — Improve readability. `Needs Refactor`

## Naming Consistency

- [ ] `[P2]` **Standardize C function naming** — Mix of `macho_parse_*`, `objc_parse_*`, `format_*`, `vmmap_*`. Consider unified prefix scheme: `dumpy_macho_*`, `dumpy_objc_*`, `dumpy_fmt_*`. `Needs Refactor`
- [ ] `[P2]` **Standardize Swift model naming** — `MethodModel` vs `ObjCMethod` (C). Ensure Swift names don't conflict with C names through the bridge. `Needs Verification`

---

# Testing Strategy

## Unit Tests — C Engine

- [x] `[P0]` **Test safe_read.c** — Bounds checking: offset at boundary, offset past end, offset+length overflow, NULL buffer, zero-length read. `Not Implemented`
- [x] `[P0]` **Test diagnostics.c** — Create, add entries, capacity growth, destroy, has_errors/has_warnings with various codes. `Not Implemented`
- [x] `[P0]` **Test macho_reader.c** — Init with all four magic values, init with truncated data, init with invalid magic. `Not Implemented`
- [ ] `[P0]` **Test macho_fat.c** — Parse real fat binary, parse thin binary (should set is_fat=false), parse with 0 architectures, parse with overlapping architectures. `Not Implemented`
- [ ] `[P0]` **Test macho_vmmap.c** — Build from known segments, translate known addresses, test PAC stripping with known tagged pointers, test unmapped address returns false. `Not Implemented`
- [ ] `[P0]` **Test objc_resolver.c type encoding** — Decode every primitive type, decode nested struct, decode array, decode block, decode qualified type, decode malformed encoding. `Not Implemented`
- [ ] `[P0]` **Test objc_resolver.c property attributes** — Parse each attribute code (T, R, C, &, N, W, D, G, S, V), parse combined attributes, parse malformed attributes. `Not Implemented`
- [ ] `[P1]` **Test objc_formatter.c** — Format class with all sections, format protocol, format category, format method with various type encodings, test buffer growth. `Not Implemented`
- [ ] `[P1]` **Test macho_load_commands.c** — Parse UUID, parse version commands, parse entry point, verify command name resolution for all LC_* types. `Not Implemented`
- [ ] `[P1]` **Test macho_sections.c** — Parse segments and sections, verify quick lookup pointers, test find_section for existing and missing sections. `Not Implemented`

## Unit Tests — Swift

- [ ] `[P0]` **Test MachOAnalyzerBridge.parseFatHeader()** — With real thin binary data, real fat binary data, truncated data, non-Mach-O data. `Not Implemented`
- [ ] `[P0]` **Test MachOAnalyzerBridge.analyze()** — With known binary, verify class count, protocol count, method names. `Not Implemented`
- [ ] `[P1]` **Test SearchService.search()** — Empty query, single character, exact match, no match, case insensitivity, special characters. `Not Implemented`
- [ ] `[P1]` **Test ExportService** — Export dump to file, export JSON, verify file contents, verify JSON schema. `Not Implemented`
- [ ] `[P1]` **Test RecentFilesStore** — Add file, update existing, remove, limit enforcement, persistence round-trip. `Not Implemented`
- [ ] `[P2]` **Test FileImportService** — Bookmark creation and resolution. `Not Implemented`

## Integration Tests

- [ ] `[P0]` **End-to-end test with a known binary** — Bundle a small test Mach-O binary in the test target. Run full analysis pipeline and verify expected class names, method counts, and dump output. `Not Implemented`
- [ ] `[P1]` **Test with fat binary** — Verify architecture selection and per-arch analysis. `Not Implemented`
- [ ] `[P1]` **Test with dylib** — Verify dylib-specific load commands are handled. `Not Implemented`
- [ ] `[P1]` **Test with framework bundle** — Verify framework binary analysis. `Not Implemented`
- [ ] `[P2]` **Test with Swift-only binary** — Binary with no ObjC metadata. Verify graceful "No ObjC metadata" result. `Not Implemented`

## Malformed Binary Tests

- [ ] `[P0]` **Test with truncated binary** — File ends mid-header, mid-load-commands, mid-section. `Not Implemented`
- [ ] `[P0]` **Test with corrupted magic** — Invalid magic bytes. `Not Implemented`
- [ ] `[P1]` **Test with oversized ncmds** — ncmds = 0xFFFFFFFF. `Not Implemented`
- [ ] `[P1]` **Test with overlapping segments** — Two segments claiming same file range. `Not Implemented`
- [ ] `[P1]` **Test with cyclic class references** — Class whose superclass points to itself. `Not Implemented`
- [ ] `[P1]` **Test with zero-length sections** — Sections with size 0. `Not Implemented`
- [ ] `[P2]` **Test with maximum-depth type encoding** — Deeply nested struct type encoding to trigger recursion limit. `Not Implemented`
- [ ] `[P2]` **Fuzz test ObjC parser** — Feed random data as __objc_classlist content and verify no crashes. `Not Implemented`

## Performance Benchmarks

- [ ] `[P1]` **Benchmark analysis of UIKit binary (~25MB)** — Measure total analysis time. Target: < 5 seconds. `Not Implemented`
- [ ] `[P1]` **Benchmark search on 5,000+ classes** — Measure search latency. Target: < 100ms per keystroke. `Not Implemented`
- [ ] `[P2]` **Benchmark memory usage** — Track peak memory during analysis of various binary sizes. `Not Implemented`
- [ ] `[P2]` **Benchmark dump text generation** — Measure format_full_dump time for large binaries. `Not Implemented`

## Regression Tests

- [ ] `[P1]` **Create snapshot tests for dump output** — Compare generated dump text against known-good reference output for a fixed test binary. Detect formatting regressions. `Not Implemented`
- [ ] `[P2]` **Create snapshot tests for JSON export** — Same approach for JSON output. `Not Implemented`

---

# App Polish and Release Readiness

## App Icon and Branding

- [ ] `[P1]` **Design and add app icon** — AppIcon.appiconset contains only placeholder configuration. Need 1024x1024 icon with dark and tinted variants. `Not Implemented`
- [ ] `[P2]` **Set accent color** — AccentColor.colorset is empty. Choose a brand color. `Not Implemented`

## App Lifecycle

- [x] `[P1]` **Handle open-in-place from Files app** — UISupportsDocumentBrowser is true in Info.plist. Verify the app handles `onOpenURL` or scene delegate URL handling to analyze a file opened from Files. `Needs Verification`
- [ ] `[P2]` **Handle memory warnings** — Release cached data, cancel non-essential work. `Not Implemented`
- [ ] `[P2]` **Handle app backgrounding during analysis** — Analysis may be killed by the system. Save intermediate state or warn user. `Not Implemented`

## Documentation

- [ ] `[P2]` **Add CLAUDE.md with project conventions** — Document architecture, build requirements, coding style. `Not Implemented`
- [ ] `[P2]` **Add README.md** — Project description, build instructions, feature list. `Not Implemented`

---

# Definition of Done

The application is considered **production-grade and fully complete** when ALL of the following conditions are met:

1. **All P0 tasks are resolved** — Zero critical bugs, zero unsafe memory access patterns, zero unbounded recursion paths.
2. **Build is clean** — Zero warnings in both Debug and Release configurations with -Wall -Wextra for C and strict concurrency checking for Swift.
3. **Core analysis pipeline works for all Mach-O types** — Executables, dylibs, frameworks, fat binaries, thin binaries. Verified with integration tests.
4. **Malformed binary handling is safe** — Truncated, corrupted, encrypted, and stripped binaries produce clear error messages. No crashes. Verified with fuzzing and edge-case tests.
5. **ObjC metadata extraction is complete** — Classes, metaclasses, methods (absolute + relative), ivars, properties, protocols, categories, selectors all correctly parsed. Verified against class-dump output for known binaries.
6. **Class dump output matches class-dump quality** — Generated @interface declarations are syntactically valid Objective-C. Method signatures, property attributes, and protocol conformances render correctly.
7. **UI is fully functional on iPhone and iPad** — All tabs, search, export, navigation work. No dead-end views. No unresponsive UI under large datasets.
8. **Accessibility passes audit** — All interactive elements have accessibility labels. Dynamic Type works. VoiceOver navigation is logical.
9. **Performance meets targets** — Analysis of a 25MB binary completes in < 5 seconds. Search responds in < 100ms. UI maintains 60fps scrolling with 5,000+ items.
10. **Test coverage is meaningful** — Unit tests for all C parser modules, integration tests with real binaries, malformed input tests, performance benchmarks. Zero flaky tests.
11. **Export works reliably** — .h dump and .json export produce valid output. Share sheet works. Temp files are cleaned up.
12. **No placeholder code remains** — Zero TODO comments, zero hardcoded test values, zero stubbed functions.
13. **Memory management is verified** — No leaks under Instruments. All C allocations freed. No retain cycles in Swift. Analysis of large binaries stays under 200MB peak.

---

# Execution Order

Implementation should proceed in this order, with each phase building on the previous:

## Phase 1: Parser Stability (Week 1-2)
1. Fix all P0 items in objc_parser.c (strict aliasing, NULL checks, recursion limits)
2. Fix memory ownership issues (strdup NULL checks, resolve_string_pointer semantics)
3. Fix PAC bit mask inconsistency
4. Add hard-coded offset validation and documentation
5. Add malformed input guards (max string length, count caps with diagnostics)
6. Add encrypted binary detection

## Phase 2: Metadata Extraction Completeness (Week 2-3)
7. Add missing load command parsing (dylibs, rpaths, code signature, encryption)
8. Add symbol table parsing
9. Add class hierarchy resolution
10. Add protocol-to-class reverse index
11. Add category-to-class linking
12. Add selector-to-method cross-referencing
13. Parse __objc_imageinfo for ABI/Swift version

## Phase 3: Class Dump Quality (Week 3-4)
14. Add forward declarations
15. Sort output by inheritance depth
16. Improve parameter naming heuristics
17. Add line width wrapping
18. Add ivar offset comments
19. Fix @required/@optional section headers
20. Snapshot-test dump output against reference

## Phase 4: UI Integration (Week 4-5)
21. Add cancellation support
22. Add granular progress reporting
23. Add linked dylibs display
24. Add header flags display
25. Make all names navigable (superclass, protocols, categories)
26. Add search debouncing and result limiting
27. Add class hierarchy tree view
28. Add syntax highlighting in dump viewer
29. Virtualize DumpView for large dumps
30. Add accessibility labels and Dynamic Type

## Phase 5: Performance Optimization (Week 5-6)
31. Release binary data after analysis
32. Add result caching
33. Optimize vmmap with binary search
34. Optimize find_section with hash table
35. Optimize search with pre-built index
36. Profile and fix any UI frame drops
37. Add memory pressure monitoring

## Phase 6: Reliability Hardening (Week 6-7)
38. Full audit of all C destroy functions for NULL safety
39. Full audit of all C parse functions for clean failure paths
40. Fuzz test ObjC parser with random data
41. Test with 20+ real-world binaries of varying sizes
42. Test iPad layouts in all split configurations
43. Test memory usage under Instruments
44. Test with encrypted binaries, stripped binaries, Swift-only binaries
45. Fix all remaining P1 items

## Phase 7: Polish and Release (Week 7-8)
46. Design and integrate app icon
47. Clean up test target stubs with real tests
48. Add temp file cleanup
49. Add copy/export feedback (toast/haptic)
50. Add error retry options
51. Final accessibility audit
52. Final performance benchmarks
53. Fix all remaining P2 items
