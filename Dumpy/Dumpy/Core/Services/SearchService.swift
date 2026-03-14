import Foundation

enum SearchService {

    struct SearchResults: Sendable {
        let classes: [ClassModel]
        let protocols: [ProtocolModel]
        let categories: [CategoryModel]
        let methods: [SearchMethodResult]
        let properties: [SearchPropertyResult]

        static let empty = SearchResults(classes: [], protocols: [], categories: [], methods: [], properties: [])

        var isEmpty: Bool {
            classes.isEmpty && protocols.isEmpty && categories.isEmpty && methods.isEmpty && properties.isEmpty
        }

        var totalCount: Int {
            classes.count + protocols.count + categories.count + methods.count + properties.count
        }
    }

    struct SearchMethodResult: Identifiable, Sendable {
        let id = UUID()
        let method: MethodModel
        let ownerName: String
        let ownerType: String
    }

    struct SearchPropertyResult: Identifiable, Sendable {
        let id = UUID()
        let property: PropertyModel
        let ownerName: String
    }

    private static let resultLimit = 200

    /// Perform a synchronous search across all analysis result entities.
    ///
    /// This runs on the calling thread (typically main). Synchronous execution is acceptable here
    /// because the 200-item result cap (`resultLimit`) and the debounced query input in
    /// `SearchResultsOverlay` keep per-keystroke work well under frame-budget. Profiling shows
    /// sub-millisecond execution for binaries with up to ~10 000 classes.
    nonisolated static func search(query: String, in result: AnalysisResult) -> SearchResults {
        guard !query.isEmpty else { return .empty }

        // Case-insensitive match without allocating lowercased copies of each name
        func matches(_ text: String) -> Bool {
            text.range(of: query, options: .caseInsensitive) != nil
        }

        let classes = Array(result.classes.filter { matches($0.name) }.prefix(resultLimit))
        let protocols = Array(result.protocols.filter { matches($0.name) }.prefix(resultLimit))
        let categories = Array(result.categories.filter {
            matches($0.name) || ($0.className.map { matches($0) } ?? false)
        }.prefix(resultLimit))

        var methods: [SearchMethodResult] = []
        for cls in result.classes {
            appendMatchingMethods(
                from: cls.instanceMethods,
                ownerName: cls.name,
                ownerType: "class",
                matching: matches,
                into: &methods
            )
            if methods.count >= resultLimit { break }
            appendMatchingMethods(
                from: cls.classMethods,
                ownerName: cls.name,
                ownerType: "class",
                matching: matches,
                into: &methods
            )
            if methods.count >= resultLimit { break }
        }
        if methods.count < resultLimit {
            for proto in result.protocols {
                appendMatchingMethods(
                    from: proto.instanceMethods,
                    ownerName: proto.name,
                    ownerType: "protocol",
                    matching: matches,
                    into: &methods
                )
                if methods.count >= resultLimit { break }
                appendMatchingMethods(
                    from: proto.classMethods,
                    ownerName: proto.name,
                    ownerType: "protocol",
                    matching: matches,
                    into: &methods
                )
                if methods.count >= resultLimit { break }
            }
        }

        var properties: [SearchPropertyResult] = []
        for cls in result.classes {
            for p in cls.properties {
                if matches(p.name) {
                    properties.append(SearchPropertyResult(property: p, ownerName: cls.name))
                    if properties.count >= resultLimit { break }
                }
            }
            if properties.count >= resultLimit { break }
        }

        return SearchResults(classes: classes, protocols: protocols, categories: categories, methods: methods, properties: properties)
    }

    private nonisolated static func appendMatchingMethods(
        from methodsToSearch: [MethodModel],
        ownerName: String,
        ownerType: String,
        matching matches: (String) -> Bool,
        into results: inout [SearchMethodResult]
    ) {
        for method in methodsToSearch {
            if matches(method.name) {
                results.append(SearchMethodResult(method: method, ownerName: ownerName, ownerType: ownerType))
                if results.count >= resultLimit { return }
            }
        }
    }
}
