import SwiftUI
import UniformTypeIdentifiers

struct HomeView: View {
    @ObservedObject var openedFileState: OpenedFileState = OpenedFileState()
    @StateObject private var recentFilesStore = RecentFilesStore()
    @StateObject private var analysisService = AnalysisService()
    @State private var showFilePicker = false
    @State private var showAnalysis = false
    @State private var showClearAllConfirmation = false

    var body: some View {
        NavigationStack {
            ScrollView {
                VStack(spacing: 24) {
                    VStack(spacing: 8) {
                        Image(systemName: "cpu")
                            .font(.system(size: 48))
                            .foregroundColor(.accentColor)
                            .accessibilityHidden(true)
                        Text("Dumpy")
                            .font(.largeTitle.bold())
                        Text("Mach-O Binary Analyzer")
                            .font(.subheadline)
                            .foregroundColor(.secondary)
                    }
                    .padding(.top, 40)

                    Button {
                        showFilePicker = true
                    } label: {
                        Label("Import Binary", systemImage: "doc.badge.plus")
                            .font(.headline)
                            .frame(maxWidth: .infinity)
                            .padding()
                            .background(Color.accentColor.opacity(0.1))
                            .foregroundColor(.accentColor)
                            .clipShape(RoundedRectangle(cornerRadius: 12))
                    }
                    .padding(.horizontal)

                    if !recentFilesStore.files.isEmpty {
                        VStack(alignment: .leading, spacing: 12) {
                            HStack {
                                Text("Recent Files")
                                    .font(.title3.bold())
                                Spacer()
                                Button(role: .destructive) {
                                    showClearAllConfirmation = true
                                } label: {
                                    Text("Clear All")
                                        .font(.subheadline)
                                }
                                .accessibilityLabel("Clear all recent files")
                            }
                            .padding(.horizontal)

                            LazyVStack(spacing: 8) {
                                ForEach(recentFilesStore.files) { file in
                                    RecentFileRow(file: file, isStale: isBookmarkStale(file)) {
                                        openRecentFile(file)
                                    }
                                    .contextMenu {
                                        Button(role: .destructive) {
                                            if let index = recentFilesStore.files.firstIndex(where: { $0.id == file.id }) {
                                                recentFilesStore.files.remove(at: index)
                                                recentFilesStore.save()
                                            }
                                        } label: {
                                            Label("Remove", systemImage: "trash")
                                        }
                                    }
                                }
                            }
                            .padding(.horizontal)
                        }
                    } else {
                        VStack(spacing: 12) {
                            Image(systemName: "doc")
                                .font(.system(size: 40))
                                .foregroundColor(.secondary)
                                .accessibilityHidden(true)
                            Text("No Recent Files")
                                .font(.headline)
                                .foregroundColor(.secondary)
                            Text("Import a Mach-O binary to get started")
                                .font(.subheadline)
                                .foregroundColor(.secondary)
                        }
                        .padding(.top, 40)
                    }
                }
            }
            .navigationTitle("")
            .navigationBarTitleDisplayMode(.inline)
            .fileImporter(isPresented: $showFilePicker, allowedContentTypes: [.data], allowsMultipleSelection: false) { result in
                handleFileImport(result)
            }
            .navigationDestination(isPresented: $showAnalysis) {
                AnalysisContainerView(service: analysisService)
            }
            .alert("Clear All Recent Files?", isPresented: $showClearAllConfirmation) {
                Button("Clear All", role: .destructive) {
                    recentFilesStore.clearAll()
                }
                Button("Cancel", role: .cancel) {}
            } message: {
                Text("This will remove all recent file entries. This action cannot be undone.")
            }
            .onChange(of: openedFileState.pendingURL) { newURL in
                guard let url = newURL else { return }
                openedFileState.pendingURL = nil
                handleOpenedFile(url)
            }
            .onDrop(of: [.data], isTargeted: nil) { providers in
                guard let provider = providers.first else { return false }
                provider.loadItem(forTypeIdentifier: UTType.data.identifier, options: nil) { item, _ in
                    guard let url = item as? URL else { return }
                    DispatchQueue.main.async {
                        handleOpenedFile(url)
                    }
                }
                return true
            }
        }
    }

    private func handleFileImport(_ result: Result<[URL], Error>) {
        guard case .success(let urls) = result, let url = urls.first else { return }

        let bookmark = FileImportService.createBookmark(for: url)
        let fileSize = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int64) ?? 0

        if let bookmark {
            recentFilesStore.addOrUpdate(fileName: url.lastPathComponent, bookmarkData: bookmark, fileSize: fileSize, architectureSummary: "")
        }

        analysisService.reset()
        analysisService.importFile(url: url)
        showAnalysis = true
    }

    private func isBookmarkStale(_ file: RecentFile) -> Bool {
        guard let resolved = FileImportService.resolveBookmark(file.bookmarkData) else { return true }
        return resolved.isStale
    }

    private func handleOpenedFile(_ url: URL) {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }

        let bookmark = FileImportService.createBookmark(for: url)
        let fileSize = (try? FileManager.default.attributesOfItem(atPath: url.path)[.size] as? Int64) ?? 0

        if let bookmark {
            recentFilesStore.addOrUpdate(fileName: url.lastPathComponent, bookmarkData: bookmark, fileSize: fileSize, architectureSummary: "")
        }

        analysisService.reset()
        analysisService.importFile(url: url)
        showAnalysis = true
    }

    private func openRecentFile(_ file: RecentFile) {
        guard let resolved = FileImportService.resolveBookmark(file.bookmarkData) else { return }
        if resolved.isStale, let newBookmark = FileImportService.createBookmark(for: resolved.url) {
            recentFilesStore.addOrUpdate(
                fileName: file.fileName,
                bookmarkData: newBookmark,
                fileSize: file.fileSize,
                architectureSummary: file.architectureSummary
            )
        }
        analysisService.reset()
        analysisService.importFile(url: resolved.url)
        showAnalysis = true
    }
}

struct RecentFileRow: View {
    let file: RecentFile
    var isStale: Bool = false
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack {
                Image(systemName: isStale ? "exclamationmark.triangle.fill" : fileIcon(for: file.fileName))
                    .font(.title3)
                    .foregroundColor(isStale ? .orange : .accentColor)
                    .frame(width: 36)
                    .accessibilityLabel(isStale ? "Stale bookmark" : fileTypeLabel(for: file.fileName))

                VStack(alignment: .leading, spacing: 4) {
                    Text(file.fileName)
                        .font(.body.bold())
                        .foregroundColor(isStale ? .secondary : .primary)
                        .lineLimit(1)
                    HStack(spacing: 8) {
                        if isStale {
                            Text("Bookmark stale")
                                .foregroundColor(.orange)
                        }
                        Text(ByteCountFormatter.string(fromByteCount: file.fileSize, countStyle: .file))
                        if !file.architectureSummary.isEmpty {
                            Text("\u{00B7}")
                            Text(file.architectureSummary)
                        }
                        Text("\u{00B7}")
                        Text(file.lastOpened, style: .relative)
                    }
                    .font(.caption)
                    .foregroundColor(.secondary)
                }

                Spacer()
                Image(systemName: "chevron.right")
                    .font(.caption)
                    .foregroundColor(Color.secondary.opacity(0.5))
                    .accessibilityHidden(true)
            }
            .padding(12)
            .background(Color(.secondarySystemBackground))
            .clipShape(RoundedRectangle(cornerRadius: 10))
            .opacity(isStale ? 0.7 : 1.0)
        }
        .buttonStyle(.plain)
    }

    private func fileIcon(for name: String) -> String {
        let lower = name.lowercased()
        if lower.hasSuffix(".dylib") { return "shippingbox.fill" }
        if lower.hasSuffix(".framework") || lower.contains(".framework") { return "square.stack.3d.up.fill" }
        if lower.hasSuffix(".a") { return "archivebox.fill" }
        if lower.hasSuffix(".o") { return "gearshape.fill" }
        return "doc.fill"
    }

    private func fileTypeLabel(for name: String) -> String {
        let lower = name.lowercased()
        if lower.hasSuffix(".dylib") { return "Dynamic library" }
        if lower.hasSuffix(".framework") || lower.contains(".framework") { return "Framework" }
        if lower.hasSuffix(".a") { return "Static library" }
        if lower.hasSuffix(".o") { return "Object file" }
        return "Binary file"
    }
}
