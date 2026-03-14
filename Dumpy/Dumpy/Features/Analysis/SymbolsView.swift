import SwiftUI

struct SymbolsView: View {
    let symbols: [SymbolModel]
    @State private var searchText = ""
    @State private var selectedGroup: SymbolGroup = .all

    enum SymbolGroup: String, CaseIterable, Identifiable {
        case all = "All"
        case external = "External"
        case local = "Local"
        case undefined = "Undefined"

        var id: String { rawValue }
    }

    private var filteredSymbols: [SymbolModel] {
        var result = symbols
        switch selectedGroup {
        case .all:
            break
        case .external:
            result = result.filter { $0.isExternal && $0.typeDescription != "undefined" }
        case .local:
            result = result.filter { !$0.isExternal && $0.typeDescription != "undefined" }
        case .undefined:
            result = result.filter { $0.typeDescription == "undefined" }
        }
        if !searchText.isEmpty {
            let query = searchText.lowercased()
            result = result.filter {
                $0.name.lowercased().contains(query) ||
                ($0.demangledName?.lowercased().contains(query) ?? false)
            }
        }
        return result
    }

    var body: some View {
        VStack(spacing: 0) {
            // Filter bar
            Picker("Group", selection: $selectedGroup) {
                ForEach(SymbolGroup.allCases) { group in
                    Text(group.rawValue).tag(group)
                }
            }
            .pickerStyle(.segmented)
            .padding(.horizontal)
            .padding(.vertical, 8)

            // Search field
            HStack {
                Image(systemName: "magnifyingglass")
                    .foregroundColor(.secondary)
                TextField("Filter symbols...", text: $searchText)
                    .textFieldStyle(.plain)
                    .autocorrectionDisabled()
                    .textInputAutocapitalization(.never)
                if !searchText.isEmpty {
                    Button {
                        searchText = ""
                    } label: {
                        Image(systemName: "xmark.circle.fill")
                            .foregroundColor(.secondary)
                    }
                    .buttonStyle(.plain)
                }
            }
            .padding(.horizontal)
            .padding(.vertical, 6)
            .background(Color(.secondarySystemBackground))
            .clipShape(RoundedRectangle(cornerRadius: 8))
            .padding(.horizontal)
            .padding(.bottom, 8)

            Divider()

            let filtered = filteredSymbols
            if filtered.isEmpty {
                VStack(spacing: 12) {
                    Image(systemName: "textformat.abc")
                        .font(.system(size: 40))
                        .foregroundColor(.secondary)
                    Text(symbols.isEmpty ? "No symbols found" : "No matching symbols")
                        .font(.subheadline)
                        .foregroundColor(.secondary)
                }
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                List {
                    Section {
                        Text("\(filtered.count) of \(symbols.count) symbols")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }

                    ForEach(filtered) { sym in
                        VStack(alignment: .leading, spacing: 2) {
                            Text(sym.displayName)
                                .font(.caption.monospaced())
                                .lineLimit(2)
                                .textSelection(.enabled)
                            if sym.isDemangled {
                                Text(sym.name)
                                    .font(.caption2.monospaced())
                                    .foregroundColor(.secondary)
                                    .lineLimit(1)
                                    .textSelection(.enabled)
                            }
                            HStack(spacing: 8) {
                                Text(sym.typeDescription)
                                    .font(.caption2)
                                    .padding(.horizontal, 5)
                                    .padding(.vertical, 1)
                                    .background(colorForType(sym.typeDescription).opacity(0.15))
                                    .foregroundColor(colorForType(sym.typeDescription))
                                    .clipShape(Capsule())
                                if sym.isExternal {
                                    Text("external")
                                        .font(.caption2)
                                        .padding(.horizontal, 5)
                                        .padding(.vertical, 1)
                                        .background(Color.blue.opacity(0.15))
                                        .foregroundColor(.blue)
                                        .clipShape(Capsule())
                                }
                                if sym.value != 0 {
                                    Text(String(format: "0x%llX", sym.value))
                                        .font(.caption2.monospaced())
                                        .foregroundColor(.secondary)
                                }
                            }
                        }
                        .padding(.vertical, 1)
                    }
                }
                .listStyle(.plain)
            }
        }
        .navigationTitle("Symbols")
    }

    private func colorForType(_ type: String) -> Color {
        switch type {
        case "section":   return .green
        case "undefined": return .orange
        case "absolute":  return .purple
        case "indirect":  return .cyan
        case "debug":     return .gray
        default:          return .secondary
        }
    }
}
