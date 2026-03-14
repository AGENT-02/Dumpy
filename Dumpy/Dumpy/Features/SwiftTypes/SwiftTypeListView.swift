import SwiftUI

struct SwiftTypeListView: View {
    let types: [SwiftTypeModel]
    @State private var filterText = ""
    @State private var kindFilter: KindFilter = .all

    enum KindFilter: String, CaseIterable, Identifiable {
        case all = "All"
        case structs = "Structs"
        case classes = "Classes"
        case enums = "Enums"

        var id: String { rawValue }
    }

    var filteredTypes: [SwiftTypeModel] {
        var result = types
        switch kindFilter {
        case .all: break
        case .structs: result = result.filter { $0.kind == .structType }
        case .classes: result = result.filter { $0.kind == .classType }
        case .enums: result = result.filter { $0.kind == .enumType }
        }
        if !filterText.isEmpty {
            result = result.filter { $0.name.localizedCaseInsensitiveContains(filterText) }
        }
        return result
    }

    var body: some View {
        Group {
            if types.isEmpty {
                EmptyStateView(icon: "swift", title: "No Swift Types", message: "No Swift type metadata found in this binary")
            } else {
                List {
                    Section {
                        HStack(spacing: 8) {
                            Image(systemName: "magnifyingglass")
                                .foregroundColor(.secondary)
                                .font(.caption)
                            TextField("Filter types", text: $filterText)
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
                        Picker("Kind", selection: $kindFilter) {
                            ForEach(KindFilter.allCases) { filter in
                                Text(filter.rawValue).tag(filter)
                            }
                        }
                        .pickerStyle(.segmented)
                    }
                    ForEach(filteredTypes) { type in
                        NavigationLink(value: AnalysisNavigationDestination.swiftTypeDetail(type)) {
                            SwiftTypeRow(type: type)
                        }
                    }
                }
            }
        }
    }
}

struct SwiftTypeRow: View {
    let type: SwiftTypeModel

    private var kindIcon: String {
        switch type.kind {
        case .structType: return "s.square"
        case .classType: return "c.square"
        case .enumType: return "e.square"
        }
    }

    private var kindColor: Color {
        switch type.kind {
        case .structType: return .blue
        case .classType: return .purple
        case .enumType: return .green
        }
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Image(systemName: kindIcon)
                    .foregroundColor(kindColor)
                    .font(.caption)
                    .accessibilityLabel(type.kind.rawValue)
                Text(type.name)
                    .font(.body.monospaced().bold())
                    .lineLimit(1)
                if !type.fields.isEmpty {
                    Text("\(type.fields.count) field\(type.fields.count == 1 ? "" : "s")")
                        .font(.caption2.bold())
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.blue.opacity(0.15))
                        .foregroundColor(.blue)
                        .clipShape(Capsule())
                        .accessibilityLabel("\(type.fields.count) fields")
                }
            }
            if !type.conformances.isEmpty {
                HStack(spacing: 4) {
                    ForEach(type.conformances.prefix(3), id: \.self) { conformance in
                        Text(conformance)
                            .font(.caption2.bold())
                            .padding(.horizontal, 5)
                            .padding(.vertical, 1)
                            .background(Color.orange.opacity(0.15))
                            .foregroundColor(.orange)
                            .clipShape(Capsule())
                            .accessibilityLabel(conformance)
                    }
                    if type.conformances.count > 3 {
                        Text("+\(type.conformances.count - 3)")
                            .font(.caption2.bold())
                            .padding(.horizontal, 5)
                            .padding(.vertical, 1)
                            .background(Color.secondary.opacity(0.12))
                            .foregroundColor(.secondary)
                            .clipShape(Capsule())
                    }
                }
            }
        }
        .padding(.vertical, 2)
    }
}
