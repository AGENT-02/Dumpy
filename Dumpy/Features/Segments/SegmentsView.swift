import SwiftUI

struct SegmentsView: View {
    let segments: [SegmentModel]
    let loadCommands: [LoadCommandModel]
    @State private var showingLoadCommands = false

    var body: some View {
        List {
            Section {
                Toggle("Show Load Commands", isOn: $showingLoadCommands)
            }

            if showingLoadCommands {
                Section("Load Commands (\(loadCommands.count))") {
                    ForEach(loadCommands) { lc in
                        LoadCommandRow(lc: lc)
                    }
                }
            }

            ForEach(segments) { seg in
                Section(header:
                    VStack(alignment: .leading, spacing: 2) {
                        HStack {
                            Text("\(seg.name) (\(seg.sections.count) sections)")
                            Spacer()
                            Text(protectionString(seg.initProtection) + " / " + protectionString(seg.maxProtection))
                                .font(.caption2.monospaced())
                        }
                        if let purpose = segmentPurpose(for: seg.name) {
                            Text(purpose)
                                .font(.caption2)
                                .foregroundColor(.secondary)
                        }
                    }
                ) {
                    VStack(alignment: .leading, spacing: 4) {
                        HStack {
                            Text("VM:").foregroundColor(.secondary)
                            Text(String(format: "0x%llX - 0x%llX", seg.vmAddress, seg.vmAddress + seg.vmSize))
                                .font(.caption.monospaced())
                        }
                        HStack {
                            Text("File:").foregroundColor(.secondary)
                            Text(String(format: "0x%llX - 0x%llX", seg.fileOffset, seg.fileOffset + seg.fileSize))
                                .font(.caption.monospaced())
                        }
                    }
                    .font(.caption)
                    .listRowBackground(segmentColor(for: seg.name))

                    ForEach(seg.sections) { sec in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(sec.name)
                                .font(.body.monospaced())
                            HStack(spacing: 8) {
                                Text(String(format: "addr: 0x%llX", sec.address))
                                Text("size: \(ByteCountFormatter.string(fromByteCount: Int64(sec.size), countStyle: .memory))")
                            }
                            .font(.caption2)
                            .foregroundColor(.secondary)
                        }
                        .listRowBackground(segmentColor(for: seg.name))
                    }
                }
            }
        }
    }

    private func segmentPurpose(for name: String) -> String? {
        switch name {
        case "__TEXT": return "Executable Code"
        case "__DATA": return "Mutable Data"
        case "__DATA_CONST": return "Immutable Data"
        case "__LINKEDIT": return "Linker Metadata"
        case "__OBJC_CONST": return "ObjC Constants"
        case "__PAGEZERO": return "Guard Page"
        default: return nil
        }
    }

    private func segmentColor(for name: String) -> Color {
        switch name {
        case "__TEXT":
            return Color.blue.opacity(0.05)
        case "__DATA":
            return Color.green.opacity(0.05)
        case "__DATA_CONST":
            return Color.green.opacity(0.03)
        case "__LINKEDIT":
            return Color.gray.opacity(0.05)
        default:
            return Color.clear
        }
    }

    private func protectionString(_ prot: UInt32) -> String {
        var s = ""
        s += (prot & 1) != 0 ? "R" : "-"
        s += (prot & 2) != 0 ? "W" : "-"
        s += (prot & 4) != 0 ? "X" : "-"
        return s
    }
}

struct LoadCommandRow: View {
    let lc: LoadCommandModel
    @State private var isExpanded = false

    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            HStack {
                Text(lc.name)
                    .font(.body.monospaced().bold())
                Spacer()
                Image(systemName: isExpanded ? "chevron.down" : "chevron.right")
                    .font(.caption)
                    .foregroundColor(.secondary)
            }
            if isExpanded {
                VStack(alignment: .leading, spacing: 4) {
                    DetailRow(label: "Command", value: String(format: "0x%X", lc.cmd))
                    DetailRow(label: "Size", value: "\(lc.cmdSize) bytes")
                    DetailRow(label: "Offset", value: String(format: "0x%llX", lc.offset))
                }
                .padding(.top, 4)
                .transition(.opacity.combined(with: .move(edge: .top)))
            }
        }
        .contentShape(Rectangle())
        .onTapGesture {
            withAnimation(.easeInOut(duration: 0.2)) {
                isExpanded.toggle()
            }
        }
    }
}

private struct DetailRow: View {
    let label: String
    let value: String

    var body: some View {
        HStack(spacing: 8) {
            Text(label + ":")
                .font(.caption)
                .foregroundColor(.secondary)
            Text(value)
                .font(.caption.monospaced())
                .foregroundColor(.secondary)
                .textSelection(.enabled)
        }
    }
}
