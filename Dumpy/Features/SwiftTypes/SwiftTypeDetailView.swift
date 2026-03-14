import SwiftUI
import UIKit

struct SwiftTypeDetailView: View {
    let type: SwiftTypeModel
    @State private var showCopied = false

    var body: some View {
        List {
            Section("Type Info") {
                InfoRow(label: "Kind", value: type.kind.rawValue)
                InfoRow(label: "Name", value: type.name)
                if let superclass = type.superclassName {
                    InfoRow(label: "Superclass", value: superclass)
                }
            }

            if !type.conformances.isEmpty {
                Section("Conformances (\(type.conformances.count))") {
                    ForEach(type.conformances, id: \.self) { conformance in
                        Text(conformance)
                            .font(.body.monospaced())
                            .textSelection(.enabled)
                    }
                }
            }

            if !type.fields.isEmpty {
                Section("Fields (\(type.fields.count))") {
                    ForEach(type.fields) { field in
                        SwiftFieldRow(field: field)
                    }
                }
            }
        }
        .navigationTitle(type.name)
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
                        UIPasteboard.general.string = type.name
                        triggerCopyFeedback()
                    } label: {
                        Label("Copy Name", systemImage: "doc.on.doc")
                    }
                    Button {
                        UIPasteboard.general.string = buildSwiftDeclaration()
                        triggerCopyFeedback()
                    } label: {
                        Label("Copy Declaration", systemImage: "doc.plaintext")
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

    private func buildSwiftDeclaration() -> String {
        var parts: [String] = []

        var header = "\(type.kind.rawValue) \(type.name)"
        var inheritanceList: [String] = []
        if let superclass = type.superclassName {
            inheritanceList.append(superclass)
        }
        inheritanceList.append(contentsOf: type.conformances)
        if !inheritanceList.isEmpty {
            header += ": " + inheritanceList.joined(separator: ", ")
        }
        header += " {"
        parts.append(header)

        for field in type.fields {
            let keyword = field.isVar ? "var" : "let"
            let typeSuffix = field.typeName.map { ": \($0)" } ?? ""
            parts.append("    \(keyword) \(field.name)\(typeSuffix)")
        }

        parts.append("}")
        return parts.joined(separator: "\n")
    }
}

struct SwiftFieldRow: View {
    let field: SwiftFieldModel

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack(spacing: 4) {
                Text(field.isVar ? "var" : "let")
                    .font(.caption.monospaced().bold())
                    .foregroundColor(field.isVar ? .orange : .blue)
                Text(field.name)
                    .font(.body.monospaced().bold())
                if let typeName = field.typeName, !typeName.isEmpty {
                    Text(": \(typeName)")
                        .font(.body.monospaced())
                        .foregroundColor(.secondary)
                }
            }
            .lineLimit(1)
            .textSelection(.enabled)
        }
        .padding(.vertical, 2)
    }
}
