import SwiftUI

struct SearchResultsOverlay: View {
    let query: String
    let result: AnalysisResult

    private let maxResultsPerSection = 50

    @State private var searchResults: SearchService.SearchResults = .empty

    var body: some View {
        Group {
        if searchResults.isEmpty {
            VStack(spacing: 12) {
                Image(systemName: "magnifyingglass")
                    .font(.system(size: 36))
                    .foregroundColor(.secondary)
                Text("No results for \"\(query)\"")
                    .font(.headline)
                    .foregroundColor(.secondary)
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity)
            .background(Color(.systemBackground))
        } else {
            List {
                if !searchResults.classes.isEmpty {
                    Section("Classes (\(searchResults.classes.count))") {
                        ForEach(searchResults.classes.prefix(maxResultsPerSection)) { cls in
                            NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                                highlightedText(cls.name, query: query)
                                    .font(.body.monospaced())
                            }
                        }
                        overflowText(total: searchResults.classes.count)
                    }
                }
                if !searchResults.protocols.isEmpty {
                    Section("Protocols (\(searchResults.protocols.count))") {
                        ForEach(searchResults.protocols.prefix(maxResultsPerSection)) { proto in
                            NavigationLink(value: AnalysisNavigationDestination.protocolDetail(proto)) {
                                highlightedText(proto.name, query: query)
                                    .font(.body.monospaced())
                            }
                        }
                        overflowText(total: searchResults.protocols.count)
                    }
                }
                if !searchResults.categories.isEmpty {
                    Section("Categories (\(searchResults.categories.count))") {
                        ForEach(searchResults.categories.prefix(maxResultsPerSection)) { cat in
                            NavigationLink(value: AnalysisNavigationDestination.categoryDetail(cat)) {
                                highlightedText("\(cat.className ?? "?") (\(cat.name))", query: query)
                                    .font(.body.monospaced())
                            }
                        }
                        overflowText(total: searchResults.categories.count)
                    }
                }
                if !searchResults.methods.isEmpty {
                    Section("Methods (\(searchResults.methods.count))") {
                        ForEach(searchResults.methods.prefix(maxResultsPerSection)) { r in
                            methodNavigationLink(for: r)
                        }
                        overflowText(total: searchResults.methods.count)
                    }
                }
                if !searchResults.properties.isEmpty {
                    Section("Properties (\(searchResults.properties.count))") {
                        ForEach(searchResults.properties.prefix(maxResultsPerSection)) { r in
                            propertyNavigationLink(for: r)
                        }
                        overflowText(total: searchResults.properties.count)
                    }
                }
            }
            .background(Color(.systemBackground))
        }
        }
        .onAppear { performSearch() }
        .onChange(of: query) { _ in performSearch() }
    }

    // MARK: - Method Navigation

    @ViewBuilder
    private func methodNavigationLink(for r: SearchService.SearchMethodResult) -> some View {
        if r.ownerType == "class", let cls = result.classes.first(where: { $0.name == r.ownerName }) {
            NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                methodResultContent(r)
            }
        } else if r.ownerType == "protocol", let proto = result.protocols.first(where: { $0.name == r.ownerName }) {
            NavigationLink(value: AnalysisNavigationDestination.protocolDetail(proto)) {
                methodResultContent(r)
            }
        } else {
            methodResultContent(r)
        }
    }

    @ViewBuilder
    private func propertyNavigationLink(for r: SearchService.SearchPropertyResult) -> some View {
        if let cls = result.classes.first(where: { $0.name == r.ownerName }) {
            NavigationLink(value: AnalysisNavigationDestination.classDetail(cls)) {
                propertyResultContent(r)
            }
        } else {
            propertyResultContent(r)
        }
    }

    private func propertyResultContent(_ r: SearchService.SearchPropertyResult) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            highlightedText(r.property.name, query: query)
                .font(.caption.monospaced())
            Text("in \(r.ownerName)")
                .font(.caption2)
                .foregroundColor(.secondary)
        }
    }

    private func methodResultContent(_ r: SearchService.SearchMethodResult) -> some View {
        VStack(alignment: .leading, spacing: 2) {
            highlightedText("\(r.method.isClassMethod ? "+" : "-") \(r.method.name)", query: query)
                .font(.caption.monospaced())
                .lineLimit(1)
            Text("in \(r.ownerType) \(r.ownerName)")
                .font(.caption2)
                .foregroundColor(.secondary)
        }
    }

    // MARK: - Highlighting

    private func highlightedText(_ text: String, query: String) -> Text {
        guard !query.isEmpty else {
            return Text(text)
        }

        let lowerText = text.lowercased()
        let lowerQuery = query.lowercased()

        var result = Text("")
        var searchStart = lowerText.startIndex

        while searchStart < lowerText.endIndex {
            if let range = lowerText.range(of: lowerQuery, range: searchStart..<lowerText.endIndex) {
                // Map range back to original text
                let origBefore = text[searchStart..<range.lowerBound]
                let origMatch = text[range.lowerBound..<range.upperBound]

                if !origBefore.isEmpty {
                    result = result + Text(origBefore)
                }
                result = result + Text(origMatch).bold().foregroundColor(.accentColor)
                searchStart = range.upperBound
            } else {
                let remaining = text[searchStart..<text.endIndex]
                result = result + Text(remaining)
                break
            }
        }

        return result
    }

    // MARK: - Overflow

    @ViewBuilder
    private func overflowText(total: Int) -> some View {
        if total > maxResultsPerSection {
            Text("and \(total - maxResultsPerSection) more...")
                .font(.caption)
                .foregroundColor(.secondary)
        }
    }

    private func performSearch() {
        searchResults = SearchService.search(query: query, in: result)
    }
}
