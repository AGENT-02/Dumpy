import SwiftUI

struct OverviewView: View {
    let result: AnalysisResult

    var body: some View {
        List {
            if !result.warnings.isEmpty {
                Section {
                    ForEach(result.warnings, id: \.self) { warning in
                        HStack(spacing: 8) {
                            Image(systemName: "exclamationmark.triangle.fill")
                                .foregroundColor(.orange)
                                .font(.body)
                                .accessibilityLabel("Warning")
                            Text("Warning: \(warning)")
                                .font(.subheadline)
                        }
                    }
                } header: {
                    Text("Warnings")
                }
            }

            Section("Binary Info") {
                InfoRow(label: "File", value: result.fileInfo.fileName)
                InfoRow(label: "Size", value: ByteCountFormatter.string(fromByteCount: Int64(result.fileInfo.fileSize), countStyle: .file))
                InfoRow(label: "Type", value: result.header.fileTypeName)
                InfoRow(label: "Architecture", value: result.selectedArchitecture)
                InfoRow(label: "64-bit", value: result.header.is64Bit ? "Yes" : "No")
                if let platform = result.platform {
                    InfoRow(label: "Platform", value: platform)
                }
                if let uuid = result.uuid {
                    InfoRow(label: "UUID", value: uuid)
                }
                if let minVer = result.minVersion {
                    InfoRow(label: "Min Version", value: minVer)
                }
                if let sdkVer = result.sdkVersion {
                    InfoRow(label: "SDK Version", value: sdkVer)
                }
                if let srcVer = result.sourceVersion {
                    InfoRow(label: "Source Version", value: srcVer)
                }
                if result.objcABIVersion > 0 {
                    InfoRow(label: "ObjC ABI Version", value: "\(result.objcABIVersion)")
                }
                if result.swiftABIVersion > 0 {
                    InfoRow(label: "Swift ABI Version", value: "\(result.swiftABIVersion)")
                }
            }

            if !result.buildTools.isEmpty {
                Section("Build Tools") {
                    ForEach(result.buildTools) { tool in
                        InfoRow(label: tool.name, value: tool.version)
                    }
                }
            }

            if !result.headerFlags.isEmpty {
                Section("Header Flags (\(result.headerFlags.count))") {
                    ForEach(result.headerFlags, id: \.self) { flag in
                        Text(flag)
                            .font(.caption.monospaced())
                            .textSelection(.enabled)
                    }
                }
            }

            if result.classes.isEmpty && result.protocols.isEmpty && result.categories.isEmpty {
                Section {
                    HStack(spacing: 8) {
                        Image(systemName: "info.circle.fill")
                            .foregroundColor(.blue)
                            .font(.body)
                            .accessibilityLabel("Notice")
                        Text("No ObjC metadata found. The binary may be stripped or Swift-only.")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                } header: {
                    Text("Notice")
                }
            }

            Section("Metadata Summary") {
                SummaryRow(icon: "c.square", label: "Classes", count: result.classes.count)
                    .accessibilityHint("Double-tap to view details")
                SummaryRow(icon: "p.square", label: "Protocols", count: result.protocols.count)
                    .accessibilityHint("Double-tap to view details")
                SummaryRow(icon: "tag", label: "Categories", count: result.categories.count)
                    .accessibilityHint("Double-tap to view details")
                SummaryRow(icon: "function", label: "Selectors", count: result.selectorCount)
                SummaryRow(icon: "textformat.abc", label: "Symbols", count: result.symbols.count)
            }

            Section("Structure") {
                SummaryRow(icon: "terminal", label: "Load Commands", count: result.loadCommands.count)
                SummaryRow(icon: "rectangle.split.3x3", label: "Segments", count: result.segments.count)
                let totalSections = result.segments.reduce(0) { $0 + $1.sections.count }
                SummaryRow(icon: "square.grid.3x3", label: "Sections", count: totalSections)
            }

            Section("Header Details") {
                InfoRow(label: "Magic", value: String(format: "0x%08X", result.header.magic))
                InfoRow(label: "CPU Type", value: String(format: "0x%X", result.header.cpuType))
                InfoRow(label: "CPU Subtype", value: String(format: "0x%X", result.header.cpuSubtype))
                InfoRow(label: "Flags", value: String(format: "0x%08X", result.header.flags))
                InfoRow(label: "Load Commands", value: "\(result.header.numberOfCommands)")
                InfoRow(label: "Commands Size", value: ByteCountFormatter.string(fromByteCount: Int64(result.header.sizeOfCommands), countStyle: .memory))
            }

            if !result.linkedLibraries.isEmpty {
                Section("Linked Libraries (\(result.linkedLibraries.count))") {
                    ForEach(result.linkedLibraries, id: \.self) { lib in
                        Text(lib)
                            .font(.caption.monospaced())
                            .textSelection(.enabled)
                            .lineLimit(2)
                    }
                }
            }

            if !result.rpaths.isEmpty {
                Section("RPATHs (\(result.rpaths.count))") {
                    ForEach(result.rpaths, id: \.self) { rpath in
                        Text(rpath)
                            .font(.caption.monospaced())
                            .textSelection(.enabled)
                            .lineLimit(2)
                    }
                }
            }

            Section("Security") {
                InfoRow(label: "Code Signed", value: result.security.hasCodeSignature ? "Yes" : "No")
                if let status = result.signingStatus {
                    InfoRow(label: "Signing Status", value: status)
                }
                InfoRow(label: "Encrypted", value: result.security.isEncrypted ? "Yes (cryptid=\(result.security.cryptId))" : "No")
            }

            if !result.diagnostics.isEmpty {
                Section("Diagnostics") {
                    ForEach(result.diagnostics) { diag in
                        HStack(spacing: 8) {
                            Image(systemName: diag.isError ? "xmark.circle.fill" : "exclamationmark.triangle.fill")
                                .foregroundColor(diag.isError ? .red : .orange)
                                .font(.caption)
                                .accessibilityLabel(diag.isError ? "Error" : "Warning")
                            Text("\(diag.isError ? "Error" : "Warning"): \(diag.message)")
                                .font(.caption)
                        }
                    }
                }
            }
        }
    }
}

struct InfoRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack {
            Text(label)
                .foregroundColor(.secondary)
            Spacer()
            Text(value)
                .font(.body.monospaced())
                .lineLimit(1)
                .minimumScaleFactor(0.7)
                .textSelection(.enabled)
        }
    }
}

struct SummaryRow: View {
    let icon: String
    let label: String
    let count: Int

    var body: some View {
        HStack {
            Image(systemName: icon)
                .foregroundColor(.accentColor)
                .frame(width: 24)
                .accessibilityHidden(true)
            Text(label)
            Spacer()
            Text("\(count)")
                .foregroundColor(.secondary)
                .font(.body.monospacedDigit())
        }
    }
}
