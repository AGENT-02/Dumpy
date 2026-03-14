import Foundation

// MARK: - Symbol Demangling

@_silgen_name("swift_demangle")
private func _swift_demangle(
    _ mangledName: UnsafePointer<CChar>?,
    _ mangledNameLength: Int,
    _ outputBuffer: UnsafeMutablePointer<CChar>?,
    _ outputBufferSize: UnsafeMutablePointer<Int>?,
    _ flags: UInt32
) -> UnsafeMutablePointer<CChar>?

@_silgen_name("__cxa_demangle")
private func _cxa_demangle(
    _ mangledName: UnsafePointer<CChar>?,
    _ outputBuffer: UnsafeMutablePointer<CChar>?,
    _ length: UnsafeMutablePointer<Int>?,
    _ status: UnsafeMutablePointer<Int32>?
) -> UnsafeMutablePointer<CChar>?

/// Attempt to demangle a symbol name.
/// Returns the demangled string, or `nil` if the name is not mangled or demangling fails.
private func demangleSymbol(_ name: String) -> String? {
    // Swift symbols: prefixed with $s, $S, _$s, or _$S
    if name.hasPrefix("$s") || name.hasPrefix("$S") ||
       name.hasPrefix("_$s") || name.hasPrefix("_$S") {
        return name.withCString { cStr in
            guard let result = _swift_demangle(cStr, name.utf8.count, nil, nil, 0) else {
                return nil
            }
            let demangled = String(cString: result)
            free(result)
            return demangled
        }
    }

    // C++ symbols: prefixed with _Z or __Z (with leading underscore on macOS)
    if name.hasPrefix("_Z") || name.hasPrefix("__Z") {
        // __cxa_demangle expects the name without the leading underscore on macOS.
        // If the name starts with "__Z", strip one underscore to get "_Z..." form.
        let mangledName = name.hasPrefix("__Z") ? String(name.dropFirst()) : name
        return mangledName.withCString { cStr in
            var status: Int32 = 0
            guard let result = _cxa_demangle(cStr, nil, nil, &status), status == 0 else {
                return nil
            }
            let demangled = String(cString: result)
            free(result)
            return demangled
        }
    }

    return nil
}

enum AnalysisError: Error, LocalizedError, Sendable {
    case fileAccessDenied
    case fileReadFailed
    case securityScopeAccessDenied
    case invalidMachO(String)
    case invalidBinary(String)
    case truncatedFile
    case unsupportedArchitecture
    case parseFailed(String)
    case noObjCMetadata
    case allocationFailed
    case fileTooLarge(Int)
    case fileImportFailed(String)
    case analysisTimedOut
    case analysisFailedGeneric(String)

    var errorDescription: String? {
        switch self {
        case .fileAccessDenied: return "Cannot access the file"
        case .fileReadFailed: return "Failed to read file data"
        case .securityScopeAccessDenied: return "Access denied. The file may have been moved or permissions changed"
        case .invalidMachO(let msg): return "Invalid Mach-O: \(msg)"
        case .invalidBinary(let msg): return "Invalid binary: \(msg)"
        case .truncatedFile: return "File appears to be truncated"
        case .unsupportedArchitecture: return "Unsupported architecture"
        case .parseFailed(let msg): return "Parse failed: \(msg)"
        case .noObjCMetadata: return "No Objective-C metadata found"
        case .allocationFailed: return "Memory allocation failed"
        case .fileTooLarge(let size): return "File too large (\(size / 1_048_576) MB). Maximum supported size is 2 GB"
        case .fileImportFailed(let msg): return "File import failed: \(msg)"
        case .analysisTimedOut: return "Analysis timed out after 60 seconds"
        case .analysisFailedGeneric(let msg): return "Analysis failed: \(msg)"
        }
    }
}

struct FatParseResult: Sendable {
    let isFat: Bool
    let architectures: [ArchitectureInfo]
}

private let analyzerQueue = DispatchQueue(label: "com.dumpy.analyzer", qos: .userInitiated)

final class MachOAnalyzerBridge: Sendable {

    /// Parse fat header to determine available architectures
    nonisolated static func parseFatHeader(data: Data) throws -> FatParseResult {
        try data.withUnsafeBytes { rawBuffer -> FatParseResult in
            // Safety: UInt8 has alignment of 1, so assumingMemoryBound(to: UInt8.self) is
            // always valid for any raw buffer base address regardless of actual alignment.
            guard let basePtr = rawBuffer.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                throw AnalysisError.fileReadFailed
            }
            let size = rawBuffer.count

            var fatInfo = FatInfo()
            let diags = diag_list_create()
            defer { diag_list_destroy(diags) }

            let result = fat_parse(basePtr, size, &fatInfo, diags)
            defer { fat_info_destroy(&fatInfo) }

            if !fatInfo.is_fat {
                // Not a fat binary - single architecture
                var ctx = MachOContext()
                let ctxResult = macho_context_init(&ctx, basePtr, size, diags)
                if ctxResult != DIAG_OK {
                    throw Self.mapDiagCode(ctxResult, diags: diags)
                }
                var headerInfo = MachOHeaderInfo()
                let hdrResult = macho_parse_header(&ctx, &headerInfo, diags)
                if hdrResult != DIAG_OK {
                    throw Self.mapDiagCode(hdrResult, diags: diags)
                }
                let archName = headerInfo.arch_name != nil ? String(cString: headerInfo.arch_name) : "unknown"
                let arch = ArchitectureInfo(cpuType: headerInfo.cpu_type, cpuSubtype: headerInfo.cpu_subtype, name: archName, offset: 0, size: UInt32(size))
                return FatParseResult(isFat: false, architectures: [arch])
            }

            if result != DIAG_OK {
                throw Self.mapDiagCode(result, diags: diags)
            }

            var archs: [ArchitectureInfo] = []
            for i in 0..<Int(fatInfo.narch) {
                let fatArch = fatInfo.archs[i]
                let name = fatArch.arch_name != nil ? String(cString: fatArch.arch_name) : "unknown"
                archs.append(ArchitectureInfo(cpuType: fatArch.cpu_type, cpuSubtype: fatArch.cpu_subtype, name: name, offset: fatArch.offset, size: fatArch.size))
            }
            return FatParseResult(isFat: true, architectures: archs)
        }
    }

    /// Full analysis of a single architecture slice
    nonisolated static func analyze(data: Data, archOffset: UInt32, archSize: UInt32, fileName: String, onProgress: (@Sendable (String) -> Void)? = nil) throws -> AnalysisResult {
        try data.withUnsafeBytes { rawBuffer -> AnalysisResult in
            // Safety: UInt8 has alignment of 1, so assumingMemoryBound(to: UInt8.self) is
            // always valid for any raw buffer base address regardless of actual alignment.
            guard let fullBase = rawBuffer.baseAddress?.assumingMemoryBound(to: UInt8.self) else {
                throw AnalysisError.fileReadFailed
            }

            guard let intOffset = Int(exactly: archOffset), let intSize = Int(exactly: archSize) else {
                throw AnalysisError.invalidBinary("Architecture offset too large")
            }

            let sliceBase = fullBase.advanced(by: intOffset)
            let sliceSize = intSize

            guard intOffset + sliceSize <= rawBuffer.count else {
                throw AnalysisError.truncatedFile
            }

            let diags = diag_list_create()
            defer { diag_list_destroy(diags) }

            // 1. Init context
            onProgress?("Parsing Mach-O header...")
            var ctx = MachOContext()
            let ctxResult = macho_context_init(&ctx, sliceBase, sliceSize, diags)
            if ctxResult != DIAG_OK {
                throw Self.mapDiagCode(ctxResult, diags: diags)
            }

            // 2. Parse header
            var headerInfo = MachOHeaderInfo()
            let hdrResult = macho_parse_header(&ctx, &headerInfo, diags)
            if hdrResult != DIAG_OK {
                throw Self.mapDiagCode(hdrResult, diags: diags)
            }

            // 3. Parse load commands
            onProgress?("Parsing load commands...")
            var lcInfo = LoadCommandsInfo()
            let lcResult = macho_parse_load_commands(&ctx, &lcInfo, diags)
            defer { load_commands_info_destroy(&lcInfo) }
            if lcResult != DIAG_OK {
                throw Self.mapDiagCode(lcResult, diags: diags)
            }

            // 3a. Check for encrypted binary
            if lcInfo.is_encrypted && lcInfo.crypt_id != 0 {
                let encMsg = "Binary is encrypted (cryptid=\(lcInfo.crypt_id)). ObjC metadata in encrypted segments may not be readable."
                encMsg.withCString { cStr in
                    diag_add(diags, DIAG_WARN_PARTIAL_METADATA, 0, cStr)
                }
            }

            // 4. Parse sections
            var sectionsInfo = SectionsInfo()
            let secResult = macho_parse_sections(&ctx, &sectionsInfo, diags)
            defer { sections_info_destroy(&sectionsInfo) }
            if secResult != DIAG_OK {
                throw Self.mapDiagCode(secResult, diags: diags)
            }

            // 5. Build VM map
            var vmmap = VMMap()
            let vmResult = vmmap_build(&sectionsInfo, &vmmap, diags)
            defer { vmmap_destroy(&vmmap) }
            if vmResult != DIAG_OK {
                throw Self.mapDiagCode(vmResult, diags: diags)
            }

            // 5a. Parse symbol table
            var symTable = SymbolTable()
            if lcInfo.has_symtab {
                let capNsyms = min(lcInfo.symtab_nsyms, 50000)
                let _ = macho_parse_symbols(&ctx,
                                            lcInfo.symtab_symoff, capNsyms,
                                            lcInfo.symtab_stroff, lcInfo.symtab_strsize,
                                            &symTable, diags)
            }
            defer { symbol_table_destroy(&symTable) }

            // 6. Parse ObjC metadata
            onProgress?("Extracting ObjC metadata...")
            var objcMetadata = ObjCMetadata()
            let objcResult = objc_parse_metadata(&ctx, &sectionsInfo, &vmmap, &objcMetadata, diags)
            defer { objc_metadata_destroy(&objcMetadata) }
            // Don't throw on missing or partial ObjC metadata - just return empty results
            if objcResult != DIAG_OK && objcResult != DIAG_ERR_NO_OBJC_METADATA && objcResult != DIAG_WARN_PARTIAL_METADATA {
                throw Self.mapDiagCode(objcResult, diags: diags)
            }

            // 6a. Parse Swift metadata
            onProgress?("Extracting Swift metadata...")
            var swiftMeta = SwiftMetadata()
            swift_parse_metadata(&ctx, &sectionsInfo, &vmmap, &swiftMeta, diags)
            defer { swift_metadata_destroy(&swiftMeta) }

            // 7. Generate dump text
            onProgress?("Generating class dump...")
            let archName = headerInfo.arch_name != nil ? String(cString: headerInfo.arch_name) : "unknown"
            let fileTypeName = headerInfo.file_type_name != nil ? String(cString: headerInfo.file_type_name) : "unknown"
            let dumpCStr = format_full_dump(&objcMetadata, fileName, archName, fileTypeName)
            let dumpText: String
            if let cStr = dumpCStr {
                dumpText = String(cString: cStr)
                free(cStr)
            } else {
                dumpText = "Failed to generate dump text"
            }

            // 7a. Generate Swift dump text
            let swiftDumpCStr = format_swift_dump(&swiftMeta, fileName, archName)
            let swiftDumpText: String
            if let cStr = swiftDumpCStr {
                swiftDumpText = String(cString: cStr)
                free(cStr)
            } else {
                swiftDumpText = ""
            }

            let combinedDump = dumpText.isEmpty ? swiftDumpText :
                (swiftDumpText.isEmpty ? dumpText : dumpText + "\n\n// MARK: - Swift Types\n\n" + swiftDumpText)

            // 7b. Map Swift types to Swift models
            var swiftTypes: [SwiftTypeModel] = []
            for i in 0..<Int(swiftMeta.type_count) {
                let t = swiftMeta.types[i]
                let kind: SwiftTypeKind
                switch t.kind {
                case UInt32(SWIFT_KIND_CLASS): kind = .classType
                case UInt32(SWIFT_KIND_STRUCT): kind = .structType
                case UInt32(SWIFT_KIND_ENUM): kind = .enumType
                default: kind = .structType
                }
                var fields: [SwiftFieldModel] = []
                if let fieldPtr = t.fields {
                    for j in 0..<Int(t.field_count) {
                        let f = fieldPtr[j]
                        fields.append(SwiftFieldModel(
                            name: f.name != nil ? String(cString: f.name) : "<unknown>",
                            typeName: f.mangled_type_name != nil ? String(cString: f.mangled_type_name) : nil,
                            isVar: f.is_var
                        ))
                    }
                }
                swiftTypes.append(SwiftTypeModel(
                    name: t.name != nil ? String(cString: t.name) : "<unknown>",
                    kind: kind,
                    superclassName: t.superclass_name != nil ? String(cString: t.superclass_name) : nil,
                    fields: fields,
                    conformances: t.conformances != nil ? Self.mapStringArray(t.conformances, count: Int(t.conformance_count)) : []
                ))
            }

            // 8. Map to Swift models
            onProgress?("Building analysis results...")
            return Self.mapToAnalysisResult(
                fileName: fileName,
                fileSize: UInt64(data.count),
                headerInfo: headerInfo,
                lcInfo: lcInfo,
                sectionsInfo: sectionsInfo,
                objcMetadata: objcMetadata,
                symTable: symTable,
                dumpText: combinedDump,
                swiftTypes: swiftTypes,
                swiftDumpText: swiftDumpText,
                diags: diags,
                archName: archName
            )
        }
    }

    // MARK: - Private Mapping

    nonisolated private static func mapToAnalysisResult(
        fileName: String, fileSize: UInt64,
        headerInfo: MachOHeaderInfo, lcInfo: LoadCommandsInfo,
        sectionsInfo: SectionsInfo, objcMetadata: ObjCMetadata,
        symTable: SymbolTable,
        dumpText: String,
        swiftTypes: [SwiftTypeModel],
        swiftDumpText: String,
        diags: UnsafeMutablePointer<DiagList>?,
        archName: String
    ) -> AnalysisResult {
        // Map header
        let header = MachOHeaderModel(
            magic: headerInfo.magic,
            cpuType: headerInfo.cpu_type,
            cpuSubtype: headerInfo.cpu_subtype,
            fileType: headerInfo.file_type,
            numberOfCommands: headerInfo.ncmds,
            sizeOfCommands: headerInfo.sizeofcmds,
            flags: headerInfo.flags,
            is64Bit: headerInfo.is_64bit,
            archName: archName,
            fileTypeName: headerInfo.file_type_name != nil ? String(cString: headerInfo.file_type_name) : "unknown"
        )

        // Map load commands
        var loadCommands: [LoadCommandModel] = []
        for i in 0..<Int(lcInfo.count) {
            let entry = lcInfo.commands[i]
            let name = entry.cmd_name != nil ? String(cString: entry.cmd_name) : String(format: "0x%X", entry.cmd)
            loadCommands.append(LoadCommandModel(cmd: entry.cmd, cmdSize: entry.cmdsize, offset: UInt64(entry.offset), name: name))
        }

        // Map segments
        var segments: [SegmentModel] = []
        for i in 0..<Int(sectionsInfo.segment_count) {
            let seg = sectionsInfo.segments[i]
            var sections: [SectionModel] = []
            for j in 0..<Int(seg.nsects) {
                let sec = seg.sections[j]
                sections.append(SectionModel(
                    name: Self.stringFromTuple(sec.sectname),
                    segmentName: Self.stringFromTuple(sec.segname),
                    address: sec.addr, size: sec.size, offset: sec.offset, flags: sec.flags
                ))
            }
            segments.append(SegmentModel(
                name: Self.stringFromTuple(seg.segname),
                vmAddress: seg.vmaddr, vmSize: seg.vmsize,
                fileOffset: seg.fileoff, fileSize: seg.filesize,
                maxProtection: seg.maxprot, initProtection: seg.initprot,
                sections: sections
            ))
        }

        // Map classes (skip metaclasses)
        var classes: [ClassModel] = []
        for i in 0..<Int(objcMetadata.class_count) {
            let cls = objcMetadata.classes[i]
            if cls.is_meta { continue }
            classes.append(Self.mapClass(cls))
        }

        // Map protocols
        var protocols: [ProtocolModel] = []
        for i in 0..<Int(objcMetadata.protocol_count) {
            protocols.append(Self.mapProtocol(objcMetadata.protocols[i]))
        }

        // Map categories
        var categories: [CategoryModel] = []
        for i in 0..<Int(objcMetadata.category_count) {
            categories.append(Self.mapCategory(objcMetadata.categories[i]))
        }

        // Count selectors without materializing every string.
        var selectorCount = 0
        for i in 0..<Int(objcMetadata.selector_count) {
            if objcMetadata.selectors[i] != nil {
                selectorCount += 1
            }
        }

        // Map linked libraries
        var linkedLibraries: [String] = []
        if let dylibNames = lcInfo.dylib_names {
            for i in 0..<Int(lcInfo.dylib_count) {
                if let name = dylibNames[i] {
                    linkedLibraries.append(String(cString: name))
                }
            }
        }

        // Map rpaths
        var rpathsList: [String] = []
        if let rpathsPtr = lcInfo.rpaths {
            for i in 0..<Int(lcInfo.rpath_count) {
                if let rpath = rpathsPtr[i] {
                    rpathsList.append(String(cString: rpath))
                }
            }
        }

        // Map security info
        let security = SecurityInfo(
            hasCodeSignature: lcInfo.has_code_signature,
            codeSigOffset: lcInfo.code_sig_offset,
            codeSigSize: lcInfo.code_sig_size,
            isEncrypted: lcInfo.is_encrypted,
            cryptId: lcInfo.crypt_id
        )

        // UUID
        var uuid: String? = nil
        if lcInfo.has_uuid {
            let u = lcInfo.uuid
            uuid = String(format: "%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
                          u.0, u.1, u.2, u.3, u.4, u.5, u.6, u.7,
                          u.8, u.9, u.10, u.11, u.12, u.13, u.14, u.15)
        }

        // Map diagnostics
        var diagnosticEntries: [DiagnosticEntry] = []
        if let diags = diags {
            for i in 0..<Int(diags.pointee.count) {
                let entry = diags.pointee.entries[i]
                let isError = entry.code.rawValue <= 8 && entry.code != DIAG_OK
                let msg = withUnsafePointer(to: entry.message) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
                }
                diagnosticEntries.append(DiagnosticEntry(isError: isError, message: msg, offset: UInt64(entry.offset)))
            }
        }

        let fileInfo = MachOFileInfo(
            fileName: fileName,
            fileSize: fileSize,
            isFat: false,
            architectures: []
        )

        // Build class hierarchy (superclass -> subclasses)
        var hierarchy: [String: [String]] = [:]
        for cls in classes {
            if let superName = cls.superclassName {
                hierarchy[superName, default: []].append(cls.name)
            }
        }

        // Build protocol conformers (protocol -> conforming classes)
        var conformers: [String: [String]] = [:]
        for cls in classes {
            for proto in cls.protocols {
                conformers[proto, default: []].append(cls.name)
            }
        }

        // Build category map (class -> categories)
        var catMap: [String: [String]] = [:]
        for cat in categories {
            if let className = cat.className {
                catMap[className, default: []].append(cat.name)
            }
        }

        // Map symbols
        var symbols: [SymbolModel] = []
        if let syms = symTable.symbols, symTable.count > 0 {
            symbols.reserveCapacity(min(Int(symTable.count), 50000))
            for i in 0..<Int(symTable.count) {
                let sym = syms[i]
                let name = sym.name != nil ? String(cString: sym.name) : "<unknown>"
                let typeName = String(cString: symbol_type_name(sym.type))
                let isExternal = sym.visibility == SYM_VIS_EXTERNAL || sym.visibility == SYM_VIS_PRIVATE_EXTERNAL
                let demangled = demangleSymbol(name)
                symbols.append(SymbolModel(
                    name: name,
                    demangledName: demangled,
                    typeDescription: typeName,
                    value: sym.value,
                    isExternal: isExternal
                ))
            }
        }

        // Map signing status
        let signingStatus: String?
        switch lcInfo.signing_status {
        case 1:  signingStatus = "Signed"
        case 2:  signingStatus = "Ad-hoc signed"
        default: signingStatus = lcInfo.has_code_signature ? "Unsigned" : nil
        }

        // Map build tools from LC_BUILD_VERSION
        var buildTools: [BuildToolModel] = []
        for i in 0..<Int(lcInfo.build_tool_count) {
            let namePtr = withUnsafePointer(to: lcInfo.build_tool_names) {
                $0.withMemoryRebound(to: UnsafeMutablePointer<CChar>?.self, capacity: 8) { $0[i] }
            }
            let verPtr = withUnsafePointer(to: lcInfo.build_tool_versions) {
                $0.withMemoryRebound(to: UnsafeMutablePointer<CChar>?.self, capacity: 8) { $0[i] }
            }
            if let namePtr = namePtr, let verPtr = verPtr {
                buildTools.append(BuildToolModel(
                    name: String(cString: namePtr),
                    version: String(cString: verPtr)
                ))
            }
        }

        // Map header flags
        let headerFlagsList = Self.decodeHeaderFlags(headerInfo.flags)

        // Map platform
        let platformName = Self.mapPlatform(lcInfo.platform)

        return AnalysisResult(
            fileInfo: fileInfo,
            header: header,
            loadCommands: loadCommands,
            segments: segments,
            classes: classes.sorted { $0.name < $1.name },
            protocols: protocols.sorted { $0.name < $1.name },
            categories: categories.sorted { ($0.className ?? "") < ($1.className ?? "") },
            selectorCount: selectorCount,
            dumpText: dumpText,
            diagnostics: diagnosticEntries,
            uuid: uuid,
            minVersion: lcInfo.min_version_string != nil ? String(cString: lcInfo.min_version_string) : nil,
            sdkVersion: lcInfo.sdk_version_string != nil ? String(cString: lcInfo.sdk_version_string) : nil,
            sourceVersion: lcInfo.source_version_string != nil ? String(cString: lcInfo.source_version_string) : nil,
            selectedArchitecture: archName,
            linkedLibraries: linkedLibraries,
            rpaths: rpathsList,
            security: security,
            symbols: symbols,
            signingStatus: signingStatus,
            headerFlags: headerFlagsList,
            platform: platformName,
            objcABIVersion: objcMetadata.has_image_info ? objcMetadata.objc_version : 0,
            swiftABIVersion: objcMetadata.has_image_info ? objcMetadata.swift_version : 0,
            buildTools: buildTools,
            swiftTypes: swiftTypes,
            swiftDumpText: swiftDumpText,
            classHierarchy: hierarchy,
            protocolConformers: conformers,
            classCategoryMap: catMap
        )
    }

    nonisolated private static func mapClass(_ cls: ObjCClassInfo) -> ClassModel {
        ClassModel(
            name: cls.name != nil ? String(cString: cls.name) : "<unknown>",
            superclassName: cls.superclass_name != nil ? String(cString: cls.superclass_name) : nil,
            isSwiftClass: cls.is_swift_class,
            instanceSize: cls.instance_size,
            instanceMethods: Self.mapMethods(cls.instance_methods, count: cls.instance_method_count, isClass: false),
            classMethods: Self.mapMethods(cls.class_methods, count: cls.class_method_count, isClass: true),
            ivars: Self.mapIvars(cls.ivars, count: cls.ivar_count),
            properties: Self.mapProperties(cls.properties, count: cls.property_count),
            protocols: Self.mapStringArray(cls.protocols, count: cls.protocol_count)
        )
    }

    nonisolated private static func mapProtocol(_ proto: ObjCProtocolInfo) -> ProtocolModel {
        ProtocolModel(
            name: proto.name != nil ? String(cString: proto.name) : "<unknown>",
            instanceMethods: Self.mapMethods(proto.instance_methods, count: proto.instance_method_count, isClass: false),
            classMethods: Self.mapMethods(proto.class_methods, count: proto.class_method_count, isClass: true),
            optionalInstanceMethods: Self.mapMethods(proto.optional_instance_methods, count: proto.optional_instance_method_count, isClass: false),
            optionalClassMethods: Self.mapMethods(proto.optional_class_methods, count: proto.optional_class_method_count, isClass: true),
            properties: Self.mapProperties(proto.properties, count: proto.property_count),
            adoptedProtocols: Self.mapStringArray(proto.adopted_protocols, count: proto.adopted_protocol_count)
        )
    }

    nonisolated private static func mapCategory(_ cat: ObjCCategoryInfo) -> CategoryModel {
        CategoryModel(
            name: cat.name != nil ? String(cString: cat.name) : "<unknown>",
            className: cat.class_name != nil ? String(cString: cat.class_name) : nil,
            instanceMethods: Self.mapMethods(cat.instance_methods, count: cat.instance_method_count, isClass: false),
            classMethods: Self.mapMethods(cat.class_methods, count: cat.class_method_count, isClass: true),
            properties: Self.mapProperties(cat.properties, count: cat.property_count),
            protocols: Self.mapStringArray(cat.protocols, count: cat.protocol_count)
        )
    }

    nonisolated private static func mapMethods(_ ptr: UnsafeMutablePointer<ObjCMethod>?, count: Int, isClass: Bool) -> [MethodModel] {
        guard let ptr = ptr, count > 0 else { return [] }
        return (0..<count).map { i in
            let m = ptr[i]
            return MethodModel(
                name: m.name != nil ? String(cString: m.name) : "<unknown>",
                typeEncoding: m.types != nil ? String(cString: m.types) : nil,
                returnType: m.return_type != nil ? String(cString: m.return_type) : nil,
                isClassMethod: isClass || m.is_class_method
            )
        }
    }

    nonisolated private static func mapIvars(_ ptr: UnsafeMutablePointer<ObjCIvar>?, count: Int) -> [IvarModel] {
        guard let ptr = ptr, count > 0 else { return [] }
        return (0..<count).map { i in
            let iv = ptr[i]
            return IvarModel(
                name: iv.name != nil ? String(cString: iv.name) : "<unknown>",
                type: iv.type != nil ? String(cString: iv.type) : nil,
                offset: iv.offset,
                size: iv.size
            )
        }
    }

    nonisolated private static func mapProperties(_ ptr: UnsafeMutablePointer<ObjCProperty>?, count: Int) -> [PropertyModel] {
        guard let ptr = ptr, count > 0 else { return [] }
        return (0..<count).map { i in
            let p = ptr[i]
            return PropertyModel(
                name: p.name != nil ? String(cString: p.name) : "<unknown>",
                attributes: p.attributes != nil ? String(cString: p.attributes) : nil,
                typeName: p.type_name != nil ? String(cString: p.type_name) : nil,
                isReadonly: p.is_readonly,
                isNonatomic: p.is_nonatomic,
                isWeak: p.is_weak,
                isCopy: p.is_copy,
                isRetain: p.is_retain
            )
        }
    }

    nonisolated private static func mapStringArray(_ ptr: UnsafeMutablePointer<UnsafeMutablePointer<CChar>?>?, count: Int) -> [String] {
        guard let ptr = ptr, count > 0 else { return [] }
        return (0..<count).compactMap { i in
            ptr[i] != nil ? String(cString: ptr[i]!) : nil
        }
    }

    nonisolated private static func stringFromCCharTuple<T>(_ tuple: T) -> String {
        withUnsafePointer(to: tuple) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: MemoryLayout<T>.size) { cStr in
                String(cString: cStr)
            }
        }
    }

    nonisolated private static func stringFromTuple(_ tuple: (CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar, CChar)) -> String {
        stringFromCCharTuple(tuple)
    }

    nonisolated private static func decodeHeaderFlags(_ flags: UInt32) -> [String] {
        let flagMap: [(UInt32, String)] = [
            (0x1,        "MH_NOUNDEFS"),
            (0x2,        "MH_INCRLINK"),
            (0x4,        "MH_DYLDLINK"),
            (0x8,        "MH_BINDATLOAD"),
            (0x10,       "MH_PREBOUND"),
            (0x20,       "MH_SPLIT_SEGS"),
            (0x40,       "MH_LAZY_INIT"),
            (0x80,       "MH_TWOLEVEL"),
            (0x100,      "MH_FORCE_FLAT"),
            (0x200,      "MH_NOMULTIDEFS"),
            (0x400,      "MH_NOFIXPREBINDING"),
            (0x800,      "MH_PREBINDABLE"),
            (0x1000,     "MH_ALLMODSBOUND"),
            (0x2000,     "MH_SUBSECTIONS_VIA_SYMBOLS"),
            (0x4000,     "MH_CANONICAL"),
            (0x8000,     "MH_WEAK_DEFINES"),
            (0x10000,    "MH_BINDS_TO_WEAK"),
            (0x20000,    "MH_ALLOW_STACK_EXECUTION"),
            (0x40000,    "MH_ROOT_SAFE"),
            (0x80000,    "MH_SETUID_SAFE"),
            (0x100000,   "MH_NO_REEXPORTED_DYLIBS"),
            (0x200000,   "MH_PIE"),
            (0x400000,   "MH_DEAD_STRIPPABLE_DYLIB"),
            (0x800000,   "MH_HAS_TLV_DESCRIPTORS"),
            (0x1000000,  "MH_NO_HEAP_EXECUTION"),
            (0x2000000,  "MH_APP_EXTENSION_SAFE"),
            (0x4000000,  "MH_NLIST_OUTOFSYNC_WITH_DYLDINFO"),
            (0x8000000,  "MH_SIM_SUPPORT"),
            (0x80000000, "MH_DYLIB_IN_CACHE"),
        ]
        var result: [String] = []
        for (bit, name) in flagMap {
            if flags & bit != 0 {
                result.append(name)
            }
        }
        return result
    }

    nonisolated private static func mapPlatform(_ platform: UInt32) -> String? {
        switch platform {
        case 1:  return "macOS"
        case 2:  return "iOS"
        case 3:  return "tvOS"
        case 4:  return "watchOS"
        case 5:  return "bridgeOS"
        case 6:  return "Mac Catalyst"
        case 7:  return "iOS Simulator"
        case 8:  return "tvOS Simulator"
        case 9:  return "watchOS Simulator"
        case 10: return "DriverKit"
        case 11: return "visionOS"
        default: return platform != 0 ? "Unknown (\(platform))" : nil
        }
    }

    nonisolated private static func mapDiagCode(_ code: DiagCode, diags: UnsafeMutablePointer<DiagList>?) -> AnalysisError {
        var messages: [String] = []
        if let diags = diags {
            for i in 0..<Int(diags.pointee.count) {
                let entry = diags.pointee.entries[i]
                let msg = withUnsafePointer(to: entry.message) { ptr in
                    ptr.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
                }
                if !msg.isEmpty { messages.append(msg) }
            }
        }
        let detail = messages.isEmpty ? "" : messages.joined(separator: "; ")

        switch code {
        case DIAG_ERR_INVALID_MAGIC: return .invalidMachO(detail)
        case DIAG_ERR_TRUNCATED: return .truncatedFile
        case DIAG_ERR_UNSUPPORTED_ARCH: return .unsupportedArchitecture
        case DIAG_ERR_NO_OBJC_METADATA: return .noObjCMetadata
        case DIAG_ERR_ALLOC_FAILED: return .allocationFailed
        default: return .parseFailed(detail.isEmpty ? "Error code \(code.rawValue)" : detail)
        }
    }
}
