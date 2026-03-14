import SwiftUI
import UIKit

struct ProtocolListView: View {
    let protocols: [ProtocolModel]
    @State private var filterText = ""

    var filtered: [ProtocolModel] {
        if filterText.isEmpty { return protocols }
        return protocols.filter { $0.name.localizedCaseInsensitiveContains(filterText) }
    }

    var body: some View {
        Group {
            if protocols.isEmpty {
                EmptyStateView(icon: "p.square", title: "No Protocols", message: "No Objective-C protocols found")
            } else {
                List {
                    Section {
                        HStack(spacing: 8) {
                            Image(systemName: "magnifyingglass")
                                .foregroundColor(.secondary)
                                .font(.caption)
                            TextField("Filter protocols", text: $filterText)
                                .font(.subheadline)
                                .textFieldStyle(.plain)
                                .autocorrectionDisabled()
                                .textInputAutocapitalization(.never)
                            if !filterText.isEmpty {
                                Button { filterText = "" } label: {
                                    Image(systemName: "xmark.circle.fill")
                                        .foregroundColor(.secondary)
                                        .font(.caption)
                                }
                            }
                        }
                    }
                    ForEach(filtered) { proto in
                        NavigationLink(value: AnalysisNavigationDestination.protocolDetail(proto)) {
                            VStack(alignment: .leading, spacing: 4) {
                                Text(proto.name)
                                    .font(.body.monospaced().bold())
                                let totalMethods = proto.instanceMethods.count + proto.classMethods.count + proto.optionalInstanceMethods.count + proto.optionalClassMethods.count
                                if totalMethods > 0 || !proto.properties.isEmpty {
                                    HStack(spacing: 12) {
                                        if totalMethods > 0 {
                                            Label("\(totalMethods) methods", systemImage: "function")
                                                .accessibilityLabel("\(totalMethods) methods")
                                        }
                                        if !proto.properties.isEmpty {
                                            Label("\(proto.properties.count) props", systemImage: "list.bullet")
                                                .accessibilityLabel("\(proto.properties.count) properties")
                                        }
                                    }
                                    .font(.caption2)
                                    .foregroundColor(.secondary.opacity(0.7))
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

struct ProtocolDetailView: View {
    let proto: ProtocolModel
    var allProtocols: [ProtocolModel] = []
    var allClasses: [ClassModel] = []
    @State private var showCopied = false

    private var conformingClasses: [ClassModel] {
        allClasses.filter { $0.protocols.contains(proto.name) }
    }

    var body: some View {
        List {
            if !proto.adoptedProtocols.isEmpty {
                Section("Adopted Protocols") {
                    ForEach(proto.adoptedProtocols, id: \.self) { name in
                        if let targetProto = allProtocols.first(where: { $0.name == name }) {
                            NavigationLink(value: AnalysisNavigationDestination.protocolDetail(targetProto)) {
                                Text(name).font(.body.monospaced())
                            }
                        } else {
                            Text(name).font(.body.monospaced())
                        }
                    }
                }
            }
            if !conformingClasses.isEmpty {
                Section("Conforming Classes (\(conformingClasses.count))") {
                    ForEach(conformingClasses) { cls in
                        NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(cls.name)
                                    .font(.body.monospaced())
                                if let superclass = cls.superclassName {
                                    Text(": \(superclass)")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                            }
                        }
                    }
                }
            }
            if !proto.properties.isEmpty {
                Section("Properties (\(proto.properties.count))") {
                    ForEach(proto.properties) { prop in PropertyRow(property: prop) }
                }
            }
            if !proto.instanceMethods.isEmpty {
                Section("Required Instance Methods (\(proto.instanceMethods.count))") {
                    ForEach(proto.instanceMethods) { m in MethodRow(method: m) }
                }
            }
            if !proto.classMethods.isEmpty {
                Section("Required Class Methods (\(proto.classMethods.count))") {
                    ForEach(proto.classMethods) { m in MethodRow(method: m) }
                }
            }
            if !proto.optionalInstanceMethods.isEmpty {
                Section("Optional Instance Methods (\(proto.optionalInstanceMethods.count))") {
                    ForEach(proto.optionalInstanceMethods) { m in OptionalMethodRow(method: m) }
                }
            }
            if !proto.optionalClassMethods.isEmpty {
                Section("Optional Class Methods (\(proto.optionalClassMethods.count))") {
                    ForEach(proto.optionalClassMethods) { m in OptionalMethodRow(method: m) }
                }
            }
        }
        .navigationTitle(proto.name)
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
                        UIPasteboard.general.string = proto.name
                        triggerCopyFeedback()
                    } label: {
                        Label("Copy Name", systemImage: "doc.on.doc")
                    }
                    Button {
                        UIPasteboard.general.string = ExportService.exportProtocolDefinition(proto)
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

struct OptionalMethodRow: View {
    let method: MethodModel
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text("\(method.isClassMethod ? "+" : "-") \(method.name)")
                .font(.body.monospaced())
                .italic()
                .foregroundColor(.secondary)
                .lineLimit(2)
                .textSelection(.enabled)
            if let ret = method.returnType, !ret.isEmpty {
                Text("returns: \(ret)")
                    .font(.caption)
                    .foregroundColor(.secondary.opacity(0.6))
            }
        }
        .padding(.vertical, 2)
    }
}
