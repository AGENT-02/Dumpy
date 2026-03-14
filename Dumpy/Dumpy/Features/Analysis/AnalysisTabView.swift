import SwiftUI

struct AnalysisTabView: View {
    let result: AnalysisResult
    @AppStorage("selectedTab") private var selectedTab: AnalysisTab = .overview
    @State private var searchText = ""
    @State private var debouncedSearchText = ""
    @State private var debounceTask: Task<Void, Never>?
    @State private var showingExportSheet = false
    @State private var exportURL: URL?

    enum AnalysisTab: String, CaseIterable, Identifiable {
        case overview = "Overview"
        case classes = "Classes"
        case hierarchy = "Hierarchy"
        case protocols = "Protocols"
        case categories = "Categories"
        case dump = "Dump"
        case segments = "Segments"
        case symbols = "Symbols"
        case swiftTypes = "Swift"

        var id: String { rawValue }

        var icon: String {
            switch self {
            case .overview: return "info.circle"
            case .classes: return "c.square"
            case .hierarchy: return "leaf"
            case .protocols: return "p.square"
            case .categories: return "tag"
            case .dump: return "doc.text"
            case .segments: return "rectangle.split.3x3"
            case .symbols: return "textformat.abc"
            case .swiftTypes: return "swift"
            }
        }
    }

    var body: some View {
        VStack(spacing: 0) {
            ScrollView(.horizontal, showsIndicators: false) {
                HStack(spacing: 4) {
                    ForEach(AnalysisTab.allCases) { tab in
                        TabButton(tab: tab, isSelected: selectedTab == tab, count: countForTab(tab)) {
                            selectedTab = tab
                        }
                    }
                }
                .padding(.horizontal)
                .padding(.vertical, 8)
            }
            .background(Color(.systemBackground))

            Divider()

            Group {
                switch selectedTab {
                case .overview:
                    OverviewView(result: result)
                case .classes:
                    ClassListView(classes: result.classes)
                case .hierarchy:
                    ClassHierarchyView(classes: result.classes)
                case .protocols:
                    ProtocolListView(protocols: result.protocols)
                case .categories:
                    CategoryListView(categories: result.categories, allClasses: result.classes)
                case .dump:
                    DumpView(dumpText: result.dumpText, fileName: result.fileInfo.fileName)
                case .segments:
                    SegmentsView(segments: result.segments, loadCommands: result.loadCommands)
                case .symbols:
                    SymbolsView(symbols: result.symbols)
                case .swiftTypes:
                    SwiftTypeListView(types: result.swiftTypes)
                }
            }
        }
        .searchable(text: $searchText, prompt: "Search classes, methods, protocols...")
        .onChange(of: searchText) { newValue in
            debounceTask?.cancel()
            debounceTask = Task { @MainActor in
                try? await Task.sleep(nanoseconds: 300_000_000)
                guard !Task.isCancelled else { return }
                debouncedSearchText = newValue
            }
        }
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button { exportDump() } label: { Label("Export Header Dump", systemImage: "doc.text") }
                    Button { exportJSON() } label: { Label("Export JSON", systemImage: "curlybraces") }
                } label: {
                    Image(systemName: "square.and.arrow.up")
                        .accessibilityLabel("Export")
                }
            }
        }
        .sheet(isPresented: $showingExportSheet) {
            if let url = exportURL {
                ShareSheetView(url: url)
            }
        }
        .overlay {
            if !debouncedSearchText.isEmpty {
                SearchResultsOverlay(query: debouncedSearchText, result: result)
            }
        }
        .onDisappear {
            debounceTask?.cancel()
            debounceTask = nil
        }
    }

    private func countForTab(_ tab: AnalysisTab) -> Int? {
        switch tab {
        case .classes: return result.classes.count
        case .protocols: return result.protocols.count
        case .categories: return result.categories.count
        case .symbols: return result.symbols.count
        case .swiftTypes: return result.swiftTypes.count
        default: return nil
        }
    }

    private func exportDump() {
        if let url = try? ExportService.exportDumpToFile(dumpText: result.dumpText, fileName: result.fileInfo.fileName) {
            exportURL = url
            showingExportSheet = true
        }
    }

    private func exportJSON() {
        if let url = try? ExportService.exportJSON(result: result, fileName: result.fileInfo.fileName) {
            exportURL = url
            showingExportSheet = true
        }
    }
}

struct TabButton: View {
    let tab: AnalysisTabView.AnalysisTab
    let isSelected: Bool
    var count: Int?
    let action: () -> Void

    var body: some View {
        Button(action: action) {
            HStack(spacing: 4) {
                Label(tab.rawValue, systemImage: tab.icon)
                if let count = count {
                    Text("\(count)")
                        .font(.caption2.bold())
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(isSelected ? Color.accentColor.opacity(0.25) : Color.secondary.opacity(0.15))
                        .clipShape(Capsule())
                }
            }
            .font(.subheadline.weight(isSelected ? .semibold : .regular))
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(isSelected ? Color.accentColor.opacity(0.15) : Color.clear)
            .foregroundColor(isSelected ? .accentColor : .secondary)
            .clipShape(Capsule())
        }
        .buttonStyle(.plain)
    }
}

struct ShareSheetView: UIViewControllerRepresentable {
    let url: URL
    func makeUIViewController(context: Context) -> UIActivityViewController {
        UIActivityViewController(activityItems: [url], applicationActivities: nil)
    }
    func updateUIViewController(_ uiViewController: UIActivityViewController, context: Context) {}
}
