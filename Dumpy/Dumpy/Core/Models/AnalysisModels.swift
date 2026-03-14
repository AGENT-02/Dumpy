import Foundation

struct MachOFileInfo: Sendable {
    let fileName: String
    let fileSize: UInt64
    let isFat: Bool
    let architectures: [ArchitectureInfo]
}

struct ArchitectureInfo: Identifiable, Sendable {
    let id = UUID()
    let cpuType: UInt32
    let cpuSubtype: UInt32
    let name: String
    let offset: UInt32
    let size: UInt32
}

struct MachOHeaderModel: Sendable {
    let magic: UInt32
    let cpuType: UInt32
    let cpuSubtype: UInt32
    let fileType: UInt32
    let numberOfCommands: UInt32
    let sizeOfCommands: UInt32
    let flags: UInt32
    let is64Bit: Bool
    let archName: String
    let fileTypeName: String
}

struct LoadCommandModel: Identifiable, Sendable {
    let id = UUID()
    let cmd: UInt32
    let cmdSize: UInt32
    let offset: UInt64
    let name: String
}

struct SegmentModel: Identifiable, Sendable {
    let id = UUID()
    let name: String
    let vmAddress: UInt64
    let vmSize: UInt64
    let fileOffset: UInt64
    let fileSize: UInt64
    let maxProtection: UInt32
    let initProtection: UInt32
    let sections: [SectionModel]
}

struct SectionModel: Identifiable, Sendable {
    let id = UUID()
    let name: String
    let segmentName: String
    let address: UInt64
    let size: UInt64
    let offset: UInt32
    let flags: UInt32
}

struct ClassModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let superclassName: String?
    let isSwiftClass: Bool
    let instanceSize: UInt32
    let instanceMethods: [MethodModel]
    let classMethods: [MethodModel]
    let ivars: [IvarModel]
    let properties: [PropertyModel]
    let protocols: [String]

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: ClassModel, rhs: ClassModel) -> Bool { lhs.id == rhs.id }
}

struct MethodModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let typeEncoding: String?
    let returnType: String?
    let isClassMethod: Bool

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: MethodModel, rhs: MethodModel) -> Bool { lhs.id == rhs.id }
}

struct IvarModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let type: String?
    let offset: UInt32
    let size: UInt32

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: IvarModel, rhs: IvarModel) -> Bool { lhs.id == rhs.id }
}

struct PropertyModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let attributes: String?
    let typeName: String?
    let isReadonly: Bool
    let isNonatomic: Bool
    let isWeak: Bool
    let isCopy: Bool
    let isRetain: Bool

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: PropertyModel, rhs: PropertyModel) -> Bool { lhs.id == rhs.id }
}

struct ProtocolModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let instanceMethods: [MethodModel]
    let classMethods: [MethodModel]
    let optionalInstanceMethods: [MethodModel]
    let optionalClassMethods: [MethodModel]
    let properties: [PropertyModel]
    let adoptedProtocols: [String]

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: ProtocolModel, rhs: ProtocolModel) -> Bool { lhs.id == rhs.id }
}

struct CategoryModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let className: String?
    let instanceMethods: [MethodModel]
    let classMethods: [MethodModel]
    let properties: [PropertyModel]
    let protocols: [String]

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: CategoryModel, rhs: CategoryModel) -> Bool { lhs.id == rhs.id }
}

struct SymbolModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let demangledName: String?
    let typeDescription: String
    let value: UInt64
    let isExternal: Bool

    /// The best available display name: demangled if available, otherwise the raw name.
    var displayName: String { demangledName ?? name }

    /// Whether this symbol has a demangled form different from its raw name.
    var isDemangled: Bool { demangledName != nil && demangledName != name }

    func hash(into hasher: inout Hasher) { hasher.combine(id) }
    static func == (lhs: SymbolModel, rhs: SymbolModel) -> Bool { lhs.id == rhs.id }
}

struct DiagnosticEntry: Identifiable, Sendable {
    let id = UUID()
    let isError: Bool
    let message: String
    let offset: UInt64
}

struct DylibInfo: Sendable {
    let name: String
    let isWeak: Bool
}

struct SecurityInfo: Sendable {
    let hasCodeSignature: Bool
    let codeSigOffset: UInt32
    let codeSigSize: UInt32
    let isEncrypted: Bool
    let cryptId: UInt32
}

enum SwiftTypeKind: String, Sendable {
    case structType = "struct"
    case classType = "class"
    case enumType = "enum"
}

struct SwiftFieldModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let typeName: String?
    let isVar: Bool

    static func == (lhs: SwiftFieldModel, rhs: SwiftFieldModel) -> Bool { lhs.id == rhs.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

struct SwiftTypeModel: Identifiable, Sendable, Hashable {
    let id = UUID()
    let name: String
    let kind: SwiftTypeKind
    let superclassName: String?
    let fields: [SwiftFieldModel]
    let conformances: [String]

    static func == (lhs: SwiftTypeModel, rhs: SwiftTypeModel) -> Bool { lhs.id == rhs.id }
    func hash(into hasher: inout Hasher) { hasher.combine(id) }
}

struct BuildToolModel: Identifiable, Sendable {
    let id = UUID()
    let name: String      // e.g. "clang", "swift", "ld"
    let version: String   // e.g. "15.0.0"
}

struct AnalysisResult: Sendable {
    let fileInfo: MachOFileInfo
    let header: MachOHeaderModel
    let loadCommands: [LoadCommandModel]
    let segments: [SegmentModel]
    let classes: [ClassModel]
    let protocols: [ProtocolModel]
    let categories: [CategoryModel]
    let selectorCount: Int
    let dumpText: String
    let diagnostics: [DiagnosticEntry]
    let uuid: String?
    let minVersion: String?
    let sdkVersion: String?
    let sourceVersion: String?
    let selectedArchitecture: String
    let linkedLibraries: [String]
    let rpaths: [String]
    let security: SecurityInfo
    let symbols: [SymbolModel]
    let signingStatus: String?

    // Header flags (e.g. MH_PIE, MH_TWOLEVEL)
    let headerFlags: [String]

    // Platform target (e.g. "iOS", "macOS")
    let platform: String?

    // Image info
    let objcABIVersion: UInt32
    let swiftABIVersion: UInt32

    // Build tools from LC_BUILD_VERSION
    let buildTools: [BuildToolModel]

    // Swift metadata
    let swiftTypes: [SwiftTypeModel]
    let swiftDumpText: String

    // Reverse indices
    let classHierarchy: [String: [String]]       // superclass name -> [subclass names]
    let protocolConformers: [String: [String]]    // protocol name -> [conforming class names]
    let classCategoryMap: [String: [String]]      // class name -> [category names]

    var hasSwiftMetadata: Bool { !swiftTypes.isEmpty }

    /// Warning-level diagnostic messages surfaced from the C engine
    /// (e.g., DIAG_WARN_PARTIAL_METADATA for encrypted or incomplete metadata).
    var warnings: [String] {
        diagnostics.filter { !$0.isError && !$0.message.isEmpty }.map(\.message)
    }
}
