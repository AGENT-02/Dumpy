import SwiftUI

struct ClassHierarchyView: View {
    let classes: [ClassModel]

    @State private var filterText = ""

    /// Builds a tree: superclass name -> [ClassModel children]
    private var childrenMap: [String: [ClassModel]] {
        var map: [String: [ClassModel]] = [:]
        for cls in classes {
            if let superName = cls.superclassName {
                map[superName, default: []].append(cls)
            }
        }
        return map
    }

    /// Root classes: those whose superclass is not in our class set,
    /// or which have no superclass.
    private var roots: [ClassModel] {
        let classNames = Set(classes.map(\.name))
        return classes.filter { cls in
            guard let superName = cls.superclassName else { return true }
            return !classNames.contains(superName)
        }.sorted { $0.name < $1.name }
    }

    /// When filtering, show only matching classes (flat).
    private var filteredClasses: [ClassModel] {
        guard !filterText.isEmpty else { return [] }
        return classes.filter { $0.name.localizedCaseInsensitiveContains(filterText) }
    }

    var body: some View {
        Group {
            if classes.isEmpty {
                EmptyStateView(icon: "leaf", title: "No Classes", message: "No class hierarchy to display")
            } else if !filterText.isEmpty {
                List {
                    Section {
                        filterField
                    }
                    ForEach(filteredClasses) { cls in
                        NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                            ClassRow(cls: cls)
                        }
                    }
                }
            } else {
                List {
                    Section {
                        filterField
                    }
                    ForEach(roots) { root in
                        HierarchyNode(cls: root, childrenMap: childrenMap, depth: 0)
                    }
                }
            }
        }
    }

    private var filterField: some View {
        HStack(spacing: 8) {
            Image(systemName: "magnifyingglass")
                .foregroundColor(.secondary)
                .font(.caption)
            TextField("Filter hierarchy", text: $filterText)
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
}

private struct HierarchyNode: View {
    let cls: ClassModel
    let childrenMap: [String: [ClassModel]]
    let depth: Int

    private var children: [ClassModel] {
        (childrenMap[cls.name] ?? []).sorted { $0.name < $1.name }
    }

    var body: some View {
        if children.isEmpty {
            NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                nodeLabel
            }
        } else {
            DisclosureGroup {
                ForEach(children) { child in
                    HierarchyNode(cls: child, childrenMap: childrenMap, depth: depth + 1)
                }
            } label: {
                NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                    nodeLabel
                }
            }
        }
    }

    private var nodeLabel: some View {
        HStack(spacing: 6) {
            Text(cls.name)
                .font(.body.monospaced())
                .lineLimit(1)
            if cls.isSwiftClass {
                Text("Swift")
                    .font(.caption2.bold())
                    .padding(.horizontal, 5)
                    .padding(.vertical, 1)
                    .background(Color.orange.opacity(0.15))
                    .foregroundColor(.orange)
                    .clipShape(Capsule())
            }
        }
    }
}
