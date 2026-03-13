import SwiftUI
import UIKit

struct ClassDetailView: View {
    let cls: ClassModel
    var allClasses: [ClassModel] = []
    @State private var showCopied = false

    /// Builds the full superclass chain starting from this class's superclass.
    /// Each entry is (name, resolvedClass?) — resolvedClass is non-nil when
    /// the class exists in the binary.
    private var superclassChain: [(name: String, resolved: ClassModel?)] {
        var chain: [(String, ClassModel?)] = []
        var visited = Set<String>()
        var currentName = cls.superclassName
        while let name = currentName, !name.isEmpty, !visited.contains(name) {
            visited.insert(name)
            if let found = allClasses.first(where: { $0.name == name }) {
                chain.append((name, found))
                currentName = found.superclassName
            } else {
                chain.append((name, nil))
                break
            }
        }
        return chain
    }

    var body: some View {
        List {
            Section("Class Info") {
                InfoRow(label: "Name", value: cls.name)
                if let superclass = cls.superclassName {
                    if let superclassCls = allClasses.first(where: { $0.name == superclass }) {
                        NavigationLink(value: superclassCls) {
                            InfoRow(label: "Superclass", value: superclass)
                        }
                        .contextMenu {
                            Button {
                                UIPasteboard.general.string = superclass
                            } label: {
                                Label("Copy", systemImage: "doc.on.doc")
                            }
                        }
                    } else {
                        InfoRow(label: "Superclass", value: superclass)
                            .contextMenu {
                                Button {
                                    UIPasteboard.general.string = superclass
                                } label: {
                                    Label("Copy", systemImage: "doc.on.doc")
                                }
                            }
                    }
                }
                InfoRow(label: "Instance Size", value: "\(cls.instanceSize) bytes")
                if !cls.protocols.isEmpty {
                    InfoRow(label: "Protocols", value: cls.protocols.joined(separator: ", "))
                }
                if cls.isSwiftClass {
                    InfoRow(label: "Swift Class", value: "Yes")
                }
            }

            if superclassChain.count > 1 {
                Section("Superclass Chain") {
                    ForEach(Array(superclassChain.enumerated()), id: \.offset) { index, entry in
                        HStack(spacing: 6) {
                            if index > 0 {
                                Image(systemName: "arrow.turn.down.right")
                                    .font(.caption2)
                                    .foregroundColor(.secondary)
                            }
                            if let resolved = entry.resolved {
                                NavigationLink(value: resolved) {
                                    Text(entry.name)
                                        .font(.body.monospaced())
                                }
                            } else {
                                Text(entry.name)
                                    .font(.body.monospaced())
                                    .foregroundColor(.secondary)
                            }
                        }
                        .padding(.leading, CGFloat(index) * 12)
                    }
                }
            }

            if !cls.properties.isEmpty {
                Section("Properties (\(cls.properties.count))") {
                    ForEach(cls.properties) { prop in PropertyRow(property: prop) }
                }
            }
            if !cls.instanceMethods.isEmpty {
                Section("Instance Methods (\(cls.instanceMethods.count))") {
                    ForEach(cls.instanceMethods) { method in MethodRow(method: method) }
                }
            }
            if !cls.classMethods.isEmpty {
                Section("Class Methods (\(cls.classMethods.count))") {
                    ForEach(cls.classMethods) { method in MethodRow(method: method) }
                }
            }
            if !cls.ivars.isEmpty {
                Section("Instance Variables (\(cls.ivars.count))") {
                    ForEach(cls.ivars) { ivar in IvarRow(ivar: ivar) }
                }
            }
        }
        .navigationTitle(cls.name)
        .navigationBarTitleDisplayMode(.inline)
        .overlay(alignment: .top) {
            if showCopied {
                Text("Copied!")
                    .font(.caption.bold())
                    .padding(.horizontal, 12)
                    .padding(.vertical, 6)
                    .background(Color(.systemGray4))
                    .clipShape(Capsule())
                    .transition(.move(edge: .top).combined(with: .opacity))
                    .padding(.top, 8)
            }
        }
        .animation(.easeInOut(duration: 0.25), value: showCopied)
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button {
                        UIPasteboard.general.string = cls.name
                        triggerCopyFeedback()
                    } label: {
                        Label("Copy Name", systemImage: "doc.on.doc")
                    }
                    Button {
                        UIPasteboard.general.string = ExportService.exportClassDefinition(cls)
                        triggerCopyFeedback()
                    } label: {
                        Label("Copy Definition", systemImage: "doc.plaintext")
                    }
                } label: {
                    Image(systemName: "doc.on.doc")
                        .accessibilityLabel("Copy to clipboard")
                }
            }
        }
    }

    private func triggerCopyFeedback() {
        let generator = UIImpactFeedbackGenerator(style: .medium)
        generator.impactOccurred()
        showCopied = true
        DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
            showCopied = false
        }
    }
}

struct MethodRow: View {
    let method: MethodModel
    @State private var isExpanded = false

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text("\(method.isClassMethod ? "+" : "-") \(method.name)")
                    .font(.body.monospaced())
                    .lineLimit(isExpanded ? nil : 2)
                    .textSelection(.enabled)
                Spacer()
                if hasExpandableDetails {
                    Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                        .font(.caption2)
                        .foregroundColor(.secondary)
                }
            }
            if isExpanded {
                VStack(alignment: .leading, spacing: 4) {
                    if let ret = method.returnType, !ret.isEmpty {
                        HStack(spacing: 4) {
                            Text("Return:")
                                .font(.caption.bold())
                                .foregroundColor(.secondary)
                            Text(ret)
                                .font(.caption.monospaced())
                                .foregroundColor(.secondary)
                        }
                    }
                    let params = extractParameterTypes()
                    if !params.isEmpty {
                        ForEach(Array(params.enumerated()), id: \.offset) { index, param in
                            HStack(spacing: 4) {
                                Text("Param \(index + 1):")
                                    .font(.caption.bold())
                                    .foregroundColor(.secondary)
                                Text(param)
                                    .font(.caption.monospaced())
                                    .foregroundColor(.secondary)
                            }
                        }
                    }
                    if let enc = method.typeEncoding, !enc.isEmpty {
                        HStack(spacing: 4) {
                            Text("Encoding:")
                                .font(.caption.bold())
                                .foregroundColor(.secondary)
                            Text(enc)
                                .font(.caption2.monospaced())
                                .foregroundColor(.secondary.opacity(0.7))
                                .textSelection(.enabled)
                        }
                    }
                }
                .padding(.top, 2)
                .transition(.opacity.combined(with: .move(edge: .top)))
            } else {
                if let ret = method.returnType, !ret.isEmpty {
                    Text("returns: \(ret)")
                        .font(.caption)
                        .foregroundColor(.secondary)
                }
            }
        }
        .padding(.vertical, 2)
        .contentShape(Rectangle())
        .onTapGesture {
            withAnimation(.easeInOut(duration: 0.2)) {
                isExpanded.toggle()
            }
        }
    }

    private var hasExpandableDetails: Bool {
        (method.returnType != nil && !(method.returnType?.isEmpty ?? true))
            || (method.typeEncoding != nil && !(method.typeEncoding?.isEmpty ?? true))
            || !extractParameterTypes().isEmpty
    }

    private func extractParameterTypes() -> [String] {
        // Parse selector parts to figure out parameter count
        let selectorParts = method.name.components(separatedBy: ":")
            .filter { !$0.isEmpty }
        let paramCount = method.name.filter { $0 == ":" }.count
        guard paramCount > 0 else { return [] }

        // Try to decode from typeEncoding if available
        if let enc = method.typeEncoding, !enc.isEmpty {
            let decoded = decodeTypeEncoding(enc)
            // First type is return, then self (@), then _cmd (:), then params
            if decoded.count > 2 + paramCount {
                // We have enough decoded types; skip return + self + _cmd
                return Array(decoded[2..<(2 + paramCount)])
            } else if decoded.count > 2 {
                return Array(decoded[2...])
            }
        }

        // Fallback: just label by selector part
        return selectorParts.prefix(paramCount).map { _ in "id" }
    }

    private func decodeTypeEncoding(_ encoding: String) -> [String] {
        var types: [String] = []
        var i = encoding.startIndex
        while i < encoding.endIndex {
            // Skip digits (stack offsets)
            if encoding[i].isNumber {
                i = encoding.index(after: i)
                continue
            }
            let decoded: String
            switch encoding[i] {
            case "@":
                // Check for "@?" (block) or "@\"ClassName\""
                let next = encoding.index(after: i)
                if next < encoding.endIndex && encoding[next] == "?" {
                    decoded = "Block"
                    i = encoding.index(after: next)
                } else if next < encoding.endIndex && encoding[next] == "\"" {
                    // Parse class name
                    let nameStart = encoding.index(after: next)
                    if let nameEnd = encoding[nameStart...].firstIndex(of: "\"") {
                        decoded = String(encoding[nameStart..<nameEnd]) + " *"
                        i = encoding.index(after: nameEnd)
                    } else {
                        decoded = "id"
                        i = next
                    }
                } else {
                    decoded = "id"
                    i = next
                }
            case "#": decoded = "Class"; i = encoding.index(after: i)
            case ":": decoded = "SEL"; i = encoding.index(after: i)
            case "c": decoded = "char"; i = encoding.index(after: i)
            case "i": decoded = "int"; i = encoding.index(after: i)
            case "s": decoded = "short"; i = encoding.index(after: i)
            case "l": decoded = "long"; i = encoding.index(after: i)
            case "q": decoded = "long long"; i = encoding.index(after: i)
            case "C": decoded = "unsigned char"; i = encoding.index(after: i)
            case "I": decoded = "unsigned int"; i = encoding.index(after: i)
            case "S": decoded = "unsigned short"; i = encoding.index(after: i)
            case "L": decoded = "unsigned long"; i = encoding.index(after: i)
            case "Q": decoded = "unsigned long long"; i = encoding.index(after: i)
            case "f": decoded = "float"; i = encoding.index(after: i)
            case "d": decoded = "double"; i = encoding.index(after: i)
            case "B": decoded = "BOOL"; i = encoding.index(after: i)
            case "v": decoded = "void"; i = encoding.index(after: i)
            case "*": decoded = "char *"; i = encoding.index(after: i)
            case "^":
                // Pointer to next type - simplified
                decoded = "void *"
                i = encoding.index(after: i)
                // Skip the pointed-to type char
                if i < encoding.endIndex && !encoding[i].isNumber {
                    i = encoding.index(after: i)
                }
            case "{":
                // Struct - find the closing brace
                if let eqIdx = encoding[i...].firstIndex(of: "=") {
                    let nameStart = encoding.index(after: i)
                    decoded = "struct " + String(encoding[nameStart..<eqIdx])
                } else {
                    decoded = "struct"
                }
                // Skip to closing brace
                var depth = 1
                i = encoding.index(after: i)
                while i < encoding.endIndex && depth > 0 {
                    if encoding[i] == "{" { depth += 1 }
                    else if encoding[i] == "}" { depth -= 1 }
                    i = encoding.index(after: i)
                }
            case "r":
                // const qualifier - skip
                i = encoding.index(after: i)
                continue
            case "n", "N", "o", "O", "R", "V":
                // qualifiers - skip
                i = encoding.index(after: i)
                continue
            default:
                decoded = String(encoding[i])
                i = encoding.index(after: i)
            }
            types.append(decoded)
        }
        return types
    }
}

struct PropertyRow: View {
    let property: PropertyModel
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text(property.name)
                    .font(.body.monospaced().bold())
                if let type = property.typeName, !type.isEmpty {
                    Text(": \(type)")
                        .font(.body.monospaced())
                        .foregroundColor(.secondary)
                }
            }
            .lineLimit(1)
            .textSelection(.enabled)
            HStack(spacing: 6) {
                if property.isNonatomic { AttributeBadge(text: "nonatomic") }
                if property.isReadonly { AttributeBadge(text: "readonly") }
                if property.isCopy { AttributeBadge(text: "copy") }
                if property.isRetain { AttributeBadge(text: "strong") }
                if property.isWeak { AttributeBadge(text: "weak") }
            }
        }
        .padding(.vertical, 2)
    }
}

struct AttributeBadge: View {
    let text: String
    var body: some View {
        Text(text)
            .font(.caption2)
            .padding(.horizontal, 5)
            .padding(.vertical, 1)
            .background(Color.secondary.opacity(0.12))
            .foregroundColor(.secondary)
            .clipShape(Capsule())
            .accessibilityLabel(text)
    }
}

struct IvarRow: View {
    let ivar: IvarModel
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text(ivar.name)
                    .font(.body.monospaced())
                if let type = ivar.type, !type.isEmpty {
                    Text(": \(type)")
                        .font(.body.monospaced())
                        .foregroundColor(.secondary)
                }
            }
            .lineLimit(1)
            .textSelection(.enabled)
            Text("offset: \(ivar.offset), size: \(ivar.size)")
                .font(.caption2)
                .foregroundColor(.secondary.opacity(0.7))
        }
        .padding(.vertical, 2)
    }
}
