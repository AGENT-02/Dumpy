import Foundation

final class ExportService: Sendable {

    private static func sanitizeFileName(_ fileName: String) -> String {
        let safe = fileName
            .replacingOccurrences(of: "/", with: "_")
            .replacingOccurrences(of: "..", with: "_")
        return String(safe.prefix(200))
    }

    nonisolated static func exportDumpToFile(dumpText: String, fileName: String) throws -> URL {
        guard !dumpText.isEmpty else {
            throw AnalysisError.parseFailed("No dump text available to export")
        }

        let safeName = sanitizeFileName(fileName)
        let tempDir = FileManager.default.temporaryDirectory
        let exportName = (safeName as NSString).deletingPathExtension + "_dump.h"
        let fileURL = tempDir.appendingPathComponent(exportName)
        try dumpText.write(to: fileURL, atomically: true, encoding: .utf8)
        return fileURL
    }

    static func exportClassDefinition(_ cls: ClassModel) -> String {
        var output = ""
        // Build @interface declaration
        output += "@interface \(cls.name)"
        if let superclass = cls.superclassName {
            output += " : \(superclass)"
        }
        if !cls.protocols.isEmpty {
            output += " <\(cls.protocols.joined(separator: ", "))>"
        }
        output += "\n"

        // Ivars
        if !cls.ivars.isEmpty {
            output += "{\n"
            for ivar in cls.ivars {
                let type = ivar.type ?? "id"
                output += "    \(type) \(ivar.name);\n"
            }
            output += "}\n"
        }
        output += "\n"

        // Properties
        for prop in cls.properties {
            var attrs: [String] = []
            if prop.isNonatomic { attrs.append("nonatomic") }
            if prop.isReadonly { attrs.append("readonly") }
            if prop.isCopy { attrs.append("copy") }
            if prop.isRetain { attrs.append("strong") }
            if prop.isWeak { attrs.append("weak") }
            let attrStr = attrs.isEmpty ? "" : "(\(attrs.joined(separator: ", "))) "
            let type = prop.typeName ?? "id"
            output += "@property \(attrStr)\(type) \(prop.name);\n"
        }
        if !cls.properties.isEmpty { output += "\n" }

        // Instance methods
        for method in cls.instanceMethods {
            let ret = method.returnType ?? "void"
            output += "- (\(ret))\(method.name);\n"
        }

        // Class methods
        for method in cls.classMethods {
            let ret = method.returnType ?? "void"
            output += "+ (\(ret))\(method.name);\n"
        }

        output += "\n@end\n"
        return output
    }

    static func exportProtocolDefinition(_ proto: ProtocolModel) -> String {
        var output = ""
        // Build @protocol declaration
        output += "@protocol \(proto.name)"
        if !proto.adoptedProtocols.isEmpty {
            output += " <\(proto.adoptedProtocols.joined(separator: ", "))>"
        }
        output += "\n\n"

        // Properties
        for prop in proto.properties {
            var attrs: [String] = []
            if prop.isNonatomic { attrs.append("nonatomic") }
            if prop.isReadonly { attrs.append("readonly") }
            if prop.isCopy { attrs.append("copy") }
            if prop.isRetain { attrs.append("strong") }
            if prop.isWeak { attrs.append("weak") }
            let attrStr = attrs.isEmpty ? "" : "(\(attrs.joined(separator: ", "))) "
            let type = prop.typeName ?? "id"
            output += "@property \(attrStr)\(type) \(prop.name);\n"
        }
        if !proto.properties.isEmpty { output += "\n" }

        // Required instance methods
        for method in proto.instanceMethods {
            let ret = method.returnType ?? "void"
            output += "- (\(ret))\(method.name);\n"
        }

        // Required class methods
        for method in proto.classMethods {
            let ret = method.returnType ?? "void"
            output += "+ (\(ret))\(method.name);\n"
        }

        // Optional methods
        let hasOptional = !proto.optionalInstanceMethods.isEmpty || !proto.optionalClassMethods.isEmpty
        if hasOptional {
            output += "\n@optional\n"
            for method in proto.optionalInstanceMethods {
                let ret = method.returnType ?? "void"
                output += "- (\(ret))\(method.name);\n"
            }
            for method in proto.optionalClassMethods {
                let ret = method.returnType ?? "void"
                output += "+ (\(ret))\(method.name);\n"
            }
        }

        output += "\n@end\n"
        return output
    }

    private static func methodJSON(_ method: MethodModel) -> [String: Any] {
        var entry: [String: Any] = [
            "name": method.name,
            "isClassMethod": method.isClassMethod
        ]
        if let ret = method.returnType { entry["returnType"] = ret }
        if let enc = method.typeEncoding { entry["typeEncoding"] = enc }
        return entry
    }

    static func exportCategoryDefinition(_ category: CategoryModel) -> String {
        var output = ""
        let className = category.className ?? "UnknownClass"
        output += "@interface \(className) (\(category.name))"
        if !category.protocols.isEmpty {
            output += " <\(category.protocols.joined(separator: ", "))>"
        }
        output += "\n\n"

        // Properties
        for prop in category.properties {
            var attrs: [String] = []
            if prop.isNonatomic { attrs.append("nonatomic") }
            if prop.isReadonly { attrs.append("readonly") }
            if prop.isCopy { attrs.append("copy") }
            if prop.isRetain { attrs.append("strong") }
            if prop.isWeak { attrs.append("weak") }
            let attrStr = attrs.isEmpty ? "" : "(\(attrs.joined(separator: ", "))) "
            let type = prop.typeName ?? "id"
            output += "@property \(attrStr)\(type) \(prop.name);\n"
        }
        if !category.properties.isEmpty { output += "\n" }

        // Instance methods
        for method in category.instanceMethods {
            let ret = method.returnType ?? "void"
            output += "- (\(ret))\(method.name);\n"
        }

        // Class methods
        for method in category.classMethods {
            let ret = method.returnType ?? "void"
            output += "+ (\(ret))\(method.name);\n"
        }

        output += "\n@end\n"
        return output
    }

    static func cleanupTempFiles() {
        let tmpDir = NSTemporaryDirectory()
        let fm = FileManager.default
        guard let files = try? fm.contentsOfDirectory(atPath: tmpDir) else { return }
        for file in files where file.hasSuffix(".h") || file.hasSuffix("_dump.json") {
            try? fm.removeItem(atPath: (tmpDir as NSString).appendingPathComponent(file))
        }
    }

    nonisolated static func exportJSON(result: AnalysisResult, fileName: String) throws -> URL {
        let safeName = sanitizeFileName(fileName)
        let tempDir = FileManager.default.temporaryDirectory
        let exportName = (safeName as NSString).deletingPathExtension + "_dump.json"
        let fileURL = tempDir.appendingPathComponent(exportName)

        var dict: [String: Any] = [
            "schema_version": "1.0",
            "fileName": result.fileInfo.fileName,
            "architecture": result.selectedArchitecture,
            "fileType": result.header.fileTypeName,
            "is64Bit": result.header.is64Bit,
        ]

        if let uuid = result.uuid { dict["uuid"] = uuid }
        if let minVer = result.minVersion { dict["minVersion"] = minVer }

        dict["classes"] = result.classes.map { cls in
            [
                "name": cls.name,
                "superclass": cls.superclassName ?? NSNull(),
                "instanceMethods": cls.instanceMethods.map { Self.methodJSON($0) },
                "classMethods": cls.classMethods.map { Self.methodJSON($0) },
                "ivarCount": cls.ivars.count,
                "propertyCount": cls.properties.count,
                "protocols": cls.protocols
            ] as [String: Any]
        }

        dict["protocols"] = result.protocols.map { proto in
            [
                "name": proto.name,
                "instanceMethods": proto.instanceMethods.map { Self.methodJSON($0) },
                "classMethods": proto.classMethods.map { Self.methodJSON($0) },
                "optionalInstanceMethods": proto.optionalInstanceMethods.map { Self.methodJSON($0) },
                "optionalClassMethods": proto.optionalClassMethods.map { Self.methodJSON($0) }
            ] as [String: Any]
        }
        dict["categories"] = result.categories.map { cat in
            [
                "name": cat.name,
                "class": cat.className ?? "",
                "instanceMethods": cat.instanceMethods.map { Self.methodJSON($0) },
                "classMethods": cat.classMethods.map { Self.methodJSON($0) }
            ] as [String: Any]
        }

        let jsonData = try JSONSerialization.data(withJSONObject: dict, options: [.prettyPrinted, .sortedKeys])
        try jsonData.write(to: fileURL, options: .atomic)
        return fileURL
    }
}
