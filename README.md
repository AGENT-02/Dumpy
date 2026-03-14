# Dumpy

A native iOS app for analyzing Mach-O binary files. Built with a C parsing engine and SwiftUI interface.

Dumpy lets you import any Mach-O binary — executables, dylibs, frameworks, object files — and inspect its structure: Objective-C metadata, Swift types, load commands, segments, symbols, and more.

## Features

- **FAT & Thin Binary Support** — Parse universal (FAT) binaries with architecture selection, or analyze single-architecture binaries directly
- **Objective-C Metadata Extraction** — Classes, protocols, categories, methods, properties, ivars, and selector references
- **Swift Type Metadata** — Structs, classes, and enums from `__swift5_types` / `__swift5_fieldmd` sections
- **Class Dump Generation** — Reconstructed `@interface` declarations with syntax highlighting, search, and export
- **Symbol Table** — Parsed nlist entries with type classification (local, external, undefined, debug)
- **Load Commands** — UUID, linked libraries, RPATHs, code signature, encryption info, build version, function starts
- **Segments & Sections** — Layout with protection flags, sizes, and section details
- **Class Hierarchy** — Visual inheritance tree with navigation
- **Cross-Reference Navigation** — Tap superclasses, protocols, conforming classes, and categories to navigate between views
- **Search** — Debounced cross-tab search across classes, methods, properties, and protocols
- **Export** — Copy class dumps to clipboard, export as `.h` header files or JSON

## Requirements

- iOS 16.6+
- Xcode 15+

## Building

1. Clone the repository
2. Open `Dumpy.xcodeproj` in Xcode
3. Select your target device or simulator
4. Build and run (Cmd+R)

No external dependencies. The C engine compiles as part of the Xcode project.

## Architecture

```
Dumpy/
├── CEngine/                    # C parsing engine
│   ├── include/                # 17 header files
│   │   ├── macho_reader.h      # Context initialization
│   │   ├── macho_fat.h         # FAT binary parsing
│   │   ├── macho_header.h      # Mach-O header parsing
│   │   ├── macho_load_commands.h
│   │   ├── macho_sections.h    # Segment/section parsing
│   │   ├── macho_symbols.h     # Symbol table parsing
│   │   ├── macho_vmmap.h       # Virtual address mapping
│   │   ├── objc_parser.h       # ObjC metadata extraction
│   │   ├── objc_resolver.h     # Type encoding & pointer resolution
│   │   ├── objc_formatter.h    # Class dump generation
│   │   ├── swift_parser.h      # Swift metadata parsing
│   │   ├── swift_formatter.h   # Swift dump generation
│   │   ├── diagnostics.h       # Error/warning system
│   │   └── safe_read.h         # Bounds-checked reads
│   └── src/                    # 14 implementation files
├── Bridge/
│   └── MachOAnalyzerBridge.swift   # C-to-Swift bridge
├── Core/
│   ├── Models/                 # Swift data models
│   └── Services/               # Analysis, search, export, file import
└── Features/
    ├── Home/                   # File picker & recent files
    ├── Analysis/               # Analysis container & tabs
    ├── Classes/                # Class list, detail, hierarchy
    ├── Protocols/              # Protocol list & detail
    ├── Categories/             # Category list & detail
    ├── SwiftTypes/             # Swift type list & detail
    ├── Dump/                   # Syntax-highlighted dump viewer
    ├── Segments/               # Segments & load commands
    └── Search/                 # Cross-tab search overlay
```

### Data Flow

```
Binary File
    → MachOAnalyzerBridge.analyze()
        → macho_context_init()         # Validate magic, detect 32/64-bit
        → macho_parse_header()         # Read header fields and flags
        → macho_parse_load_commands()  # Extract LCs, dylibs, RPATHs, UUID
        → macho_parse_sections()       # Parse segments and sections
        → vmmap_build()                # Build virtual address map
        → objc_parse_metadata()        # Extract classes, protocols, categories
        → macho_parse_symbols()        # Parse symbol table
        → swift_parse_metadata()       # Parse Swift type descriptors
        → format_full_dump()           # Generate class dump text
    → AnalysisResult (Swift)           # Mapped to Swift models
        → AnalysisTabView              # Displayed in 9-tab UI
```

## Testing

```bash
xcodebuild -project Dumpy.xcodeproj \
  -scheme Dumpy \
  -destination 'platform=iOS Simulator,name=iPhone 17 Pro' \
  test
```

Test suites cover:
- Safe reads and bounds checking
- Diagnostics system
- Mach-O header parsing (32/64-bit, CIGAM)
- FAT binary parsing (thin, multi-arch, truncated, invalid)
- Type encoding decoding
- Property attribute parsing
- VM address mapping
- Search service
- Bridge integration

## Supported Binary Types

| Type | Extension | Description |
|------|-----------|-------------|
| Executable | — | Main app binaries |
| Dynamic Library | `.dylib` | Shared libraries |
| Framework | `.framework` | Framework bundles |
| Static Library | `.a` | Archive libraries |
| Object File | `.o` | Compiled object files |
| Bundle | `.bundle` | Loadable bundles |

## License

MIT License
