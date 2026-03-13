import SwiftUI
import UIKit

struct CategoryListView: View {
    let categories: [CategoryModel]
    var allClasses: [ClassModel] = []
    @State private var filterText = ""

    var filtered: [CategoryModel] {
        if filterText.isEmpty { return categories }
        return categories.filter {
            $0.name.localizedCaseInsensitiveContains(filterText) ||
            ($0.className?.localizedCaseInsensitiveContains(filterText) ?? false)
        }
    }

    var body: some View {
        Group {
            if categories.isEmpty {
                EmptyStateView(icon: "tag", title: "No Categories", message: "No Objective-C categories found")
            } else {
                List {
                    Section {
                        HStack(spacing: 8) {
                            Image(systemName: "magnifyingglass")
                                .foregroundColor(.secondary)
                                .font(.caption)
                            TextField("Filter categories", text: $filterText)
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
                    ForEach(filtered) { cat in
                        NavigationLink(value: cat) {
                            VStack(alignment: .leading, spacing: 4) {
                                if let className = cat.className {
                                    Text("\(className) (\(cat.name))")
                                        .font(.body.monospaced().bold())
                                } else {
                                    Text(cat.name)
                                        .font(.body.monospaced().bold())
                                }
                                let total = cat.instanceMethods.count + cat.classMethods.count
                                if total > 0 {
                                    Label("\(total) methods", systemImage: "function")
                                        .font(.caption2)
                                        .foregroundColor(.secondary.opacity(0.7))
                                        .accessibilityLabel("\(total) methods")
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

struct CategoryDetailView: View {
    let category: CategoryModel
    var allClasses: [ClassModel] = []
    var allCategories: [CategoryModel] = []
    @State private var showCopied = false

    private var relatedCategories: [CategoryModel] {
        guard let className = category.className else { return [] }
        return allCategories.filter { $0.className == className && $0.id != category.id }
    }

    var body: some View {
        List {
            Section("Category Info") {
                InfoRow(label: "Name", value: category.name)
                if let cls = category.className {
                    if let targetClass = allClasses.first(where: { $0.name == cls }) {
                        NavigationLink(value: targetClass) {
                            InfoRow(label: "Class", value: cls)
                        }
                    } else {
                        InfoRow(label: "Class", value: cls)
                    }
                }
            }
            if !category.properties.isEmpty {
                Section("Properties (\(category.properties.count))") {
                    ForEach(category.properties) { prop in PropertyRow(property: prop) }
                }
            }
            if !category.instanceMethods.isEmpty {
                Section("Instance Methods (\(category.instanceMethods.count))") {
                    ForEach(category.instanceMethods) { m in MethodRow(method: m) }
                }
            }
            if !category.classMethods.isEmpty {
                Section("Class Methods (\(category.classMethods.count))") {
                    ForEach(category.classMethods) { m in MethodRow(method: m) }
                }
            }
            if !relatedCategories.isEmpty {
                Section("Other Categories for \(category.className ?? "this class")") {
                    ForEach(relatedCategories) { cat in
                        NavigationLink(value: cat) {
                            VStack(alignment: .leading, spacing: 2) {
                                Text(cat.name)
                                    .font(.body.monospaced())
                                let total = cat.instanceMethods.count + cat.classMethods.count
                                if total > 0 {
                                    Text("\(total) methods")
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                            }
                        }
                    }
                }
            }
        }
        .navigationTitle(category.className.map { "\($0) (\(category.name))" } ?? category.name)
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
                Button {
                    UIPasteboard.general.string = ExportService.exportCategoryDefinition(category)
                    let generator = UIImpactFeedbackGenerator(style: .medium)
                    generator.impactOccurred()
                    showCopied = true
                    DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                        showCopied = false
                    }
                } label: {
                    Image(systemName: "doc.on.doc")
                        .accessibilityLabel("Copy definition")
                }
            }
        }
    }
}
