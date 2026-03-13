import SwiftUI

struct ClassListView: View {
    let classes: [ClassModel]
    @State private var filterText = ""

    var filteredClasses: [ClassModel] {
        if filterText.isEmpty { return classes }
        return classes.filter { $0.name.localizedCaseInsensitiveContains(filterText) }
    }

    var body: some View {
        Group {
            if classes.isEmpty {
                EmptyStateView(icon: "c.square", title: "No Classes", message: "No Objective-C classes found in this binary")
            } else {
                List {
                    Section {
                        HStack(spacing: 8) {
                            Image(systemName: "magnifyingglass")
                                .foregroundColor(.secondary)
                                .font(.caption)
                            TextField("Filter classes", text: $filterText)
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
                    ForEach(filteredClasses) { cls in
                        NavigationLink(value: cls) {
                            ClassRow(cls: cls)
                        }
                    }
                }
            }
        }
    }
}

struct ClassRow: View {
    let cls: ClassModel

    var body: some View {
        VStack(alignment: .leading, spacing: 4) {
            HStack(spacing: 6) {
                Text(cls.name)
                    .font(.body.monospaced().bold())
                    .lineLimit(1)
                if cls.isSwiftClass {
                    Text("Swift")
                        .font(.caption2.bold())
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.orange.opacity(0.15))
                        .foregroundColor(.orange)
                        .clipShape(Capsule())
                        .accessibilityLabel("Swift class")
                }
                if !cls.protocols.isEmpty {
                    Text("\(cls.protocols.count) protocol\(cls.protocols.count == 1 ? "" : "s")")
                        .font(.caption2.bold())
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(Color.purple.opacity(0.15))
                        .foregroundColor(.purple)
                        .clipShape(Capsule())
                        .accessibilityLabel("\(cls.protocols.count) protocols")
                }
            }
            if let superclass = cls.superclassName {
                Text(": \(superclass)")
                    .font(.caption.monospaced())
                    .foregroundColor(.secondary)
            }
            HStack(spacing: 12) {
                let methodCount = cls.instanceMethods.count + cls.classMethods.count
                if methodCount > 0 {
                    Label("\(methodCount) methods", systemImage: "function")
                        .accessibilityLabel("\(methodCount) methods")
                }
                if !cls.properties.isEmpty {
                    Label("\(cls.properties.count) props", systemImage: "list.bullet")
                        .accessibilityLabel("\(cls.properties.count) properties")
                }
                if !cls.ivars.isEmpty {
                    Label("\(cls.ivars.count) ivars", systemImage: "curlybraces")
                        .accessibilityLabel("\(cls.ivars.count) instance variables")
                }
            }
            .font(.caption2)
            .foregroundColor(.secondary.opacity(0.7))
        }
        .padding(.vertical, 2)
    }
}

struct EmptyStateView: View {
    let icon: String
    let title: String
    let message: String

    var body: some View {
        VStack(spacing: 12) {
            Image(systemName: icon)
                .font(.system(size: 40))
                .foregroundColor(.secondary)
                .accessibilityHidden(true)
            Text(title)
                .font(.headline)
                .foregroundColor(.secondary)
            Text(message)
                .font(.subheadline)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
        .padding()
    }
}
