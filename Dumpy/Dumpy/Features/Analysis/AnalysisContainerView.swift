import SwiftUI

struct AnalysisContainerView: View {
    @ObservedObject var service: AnalysisService

    var body: some View {
        Group {
            switch service.phase {
            case .idle, .reading, .parsingFatHeader, .analyzing:
                ProgressContentView(message: service.progress)
            case .selectingArchitecture:
                ArchitecturePickerView(service: service)
            case .complete:
                if let result = service.result {
                    AnalysisTabView(result: result)
                        .id("analysis-tabs")
                }
            case .failed(let error):
                ErrorContentView(error: error) {
                    service.reset()
                }
            }
        }
        .navigationTitle(service.fileInfo?.fileName ?? "Analysis")
        .navigationBarTitleDisplayMode(.inline)
        .navigationDestination(for: ClassModel.self) { cls in
            ClassDetailView(cls: cls, allClasses: service.result?.classes ?? [])
        }
        .navigationDestination(for: ProtocolModel.self) { proto in
            ProtocolDetailView(proto: proto, allProtocols: service.result?.protocols ?? [], allClasses: service.result?.classes ?? [])
        }
        .navigationDestination(for: CategoryModel.self) { cat in
            CategoryDetailView(category: cat, allClasses: service.result?.classes ?? [], allCategories: service.result?.categories ?? [])
        }
    }
}

struct ProgressContentView: View {
    let message: String
    var onCancel: (() -> Void)?

    var body: some View {
        VStack(spacing: 16) {
            ProgressView()
                .controlSize(.large)
            Text(message)
                .font(.subheadline)
                .foregroundColor(.secondary)
            if let onCancel = onCancel {
                Button("Cancel", role: .cancel, action: onCancel)
                    .buttonStyle(.bordered)
                    .padding(.top, 8)
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}

struct ErrorContentView: View {
    let error: AnalysisError
    let onDismiss: () -> Void
    var onRetry: (() -> Void)?

    private var suggestion: String? {
        let desc = error.localizedDescription.lowercased()
        if desc.contains("encrypt") {
            return "Encrypted binaries cannot be analyzed. A decrypted copy is required."
        }
        if desc.contains("magic") || desc.contains("not a mach-o") || desc.contains("invalid mach-o") {
            return "This file does not appear to be a Mach-O binary."
        }
        if desc.contains("truncated") {
            return "The file may be incomplete or corrupted. Try re-downloading or re-copying it."
        }
        if desc.contains("too large") {
            return "Try analyzing a thinner (single-architecture) slice of the binary."
        }
        if desc.contains("access denied") || desc.contains("permissions changed") {
            return "The app could not obtain sandbox access to this file. Try re-importing it from Files."
        }
        return nil
    }

    var body: some View {
        VStack(spacing: 16) {
            Image(systemName: "exclamationmark.triangle")
                .font(.system(size: 48))
                .foregroundColor(.red)
                .accessibilityLabel("Error")
            Text("Analysis Failed")
                .font(.title2.bold())
            Text(error.localizedDescription)
                .font(.body)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .padding(.horizontal)
            if let suggestion = suggestion {
                Text(suggestion)
                    .font(.callout)
                    .foregroundColor(.secondary)
                    .multilineTextAlignment(.center)
                    .padding(.horizontal, 32)
            }
            HStack(spacing: 16) {
                Button("Go Back", action: onDismiss)
                    .buttonStyle(.bordered)
                if let onRetry = onRetry {
                    Button("Retry", action: onRetry)
                        .buttonStyle(.borderedProminent)
                }
            }
        }
    }
}

struct ArchitecturePickerView: View {
    @ObservedObject var service: AnalysisService

    var body: some View {
        VStack(spacing: 24) {
            VStack(spacing: 8) {
                Image(systemName: "cpu")
                    .font(.system(size: 40))
                    .foregroundColor(.accentColor)
                    .accessibilityHidden(true)
                Text("Universal Binary")
                    .font(.title2.bold())
                Text("Select an architecture to analyze")
                    .font(.subheadline)
                    .foregroundColor(.secondary)
            }
            .padding(.top, 40)

            if let archs = service.fileInfo?.architectures {
                VStack(spacing: 8) {
                    ForEach(Array(archs.enumerated()), id: \.element.id) { index, arch in
                        Button {
                            service.selectArchitecture(index)
                        } label: {
                            HStack {
                                Image(systemName: "chip")
                                    .foregroundColor(.accentColor)
                                    .accessibilityHidden(true)
                                VStack(alignment: .leading) {
                                    Text(arch.name)
                                        .font(.headline)
                                    Text(ByteCountFormatter.string(fromByteCount: Int64(arch.size), countStyle: .file))
                                        .font(.caption)
                                        .foregroundColor(.secondary)
                                }
                                Spacer()
                                Image(systemName: "chevron.right")
                                    .foregroundColor(Color.secondary.opacity(0.5))
                                    .accessibilityHidden(true)
                            }
                            .padding()
                            .background(Color(.secondarySystemBackground))
                            .clipShape(RoundedRectangle(cornerRadius: 10))
                        }
                        .buttonStyle(.plain)
                    }
                }
                .padding(.horizontal)
            }

            Spacer()
        }
    }
}
