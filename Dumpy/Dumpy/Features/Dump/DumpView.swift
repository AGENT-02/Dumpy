import SwiftUI
import UIKit

enum DumpFontSize: String, CaseIterable, Identifiable {
    case small = "Small"
    case medium = "Medium"
    case large = "Large"

    var id: String { rawValue }

    var lineFont: Font {
        switch self {
        case .small: return .system(size: 10, design: .monospaced)
        case .medium: return .system(size: 13, design: .monospaced)
        case .large: return .system(size: 16, design: .monospaced)
        }
    }

    var lineNumberFont: Font {
        switch self {
        case .small: return .system(size: 8, design: .monospaced)
        case .medium: return .system(size: 10, design: .monospaced)
        case .large: return .system(size: 12, design: .monospaced)
        }
    }
}

struct DumpView: View {
    let dumpText: String
    let fileName: String
    @State private var showCopied = false
    @State private var searchText = ""
    @State private var debouncedSearch = ""
    @State private var debounceTask: Task<Void, Never>?
    @State private var cachedLines: [String] = []
    @State private var highlightedIndices: Set<Int> = []
    @State private var matchCount: Int = 0
    @AppStorage("dumpFontSize") private var fontSizeRaw: String = DumpFontSize.medium.rawValue

    private var fontSize: DumpFontSize {
        DumpFontSize(rawValue: fontSizeRaw) ?? .medium
    }

    var body: some View {
        Group {
            if dumpText.isEmpty {
                EmptyStateView(icon: "doc.text", title: "No Dump Output", message: "No Objective-C metadata to dump")
            } else {
                ScrollViewReader { proxy in
                    ScrollView(.vertical) {
                        ScrollView(.horizontal, showsIndicators: false) {
                            LazyVStack(alignment: .leading, spacing: 0) {
                                Color.clear.frame(height: 0).id("dumpTop")
                                ForEach(Array(cachedLines.enumerated()), id: \.offset) { index, line in
                                    DumpLineView(
                                        lineNumber: index + 1,
                                        text: line,
                                        isHighlighted: highlightedIndices.contains(index),
                                        fontSize: fontSize
                                    )
                                }
                            }
                            .padding()
                        }
                    }
                    .onAppear {
                        if cachedLines.isEmpty {
                            cachedLines = dumpText.components(separatedBy: "\n")
                        }
                        DispatchQueue.main.async {
                            proxy.scrollTo("dumpTop", anchor: .top)
                        }
                    }
                }
                .safeAreaInset(edge: .top) {
                    if !dumpText.isEmpty {
                        HStack(spacing: 8) {
                            Image(systemName: "magnifyingglass")
                                .foregroundColor(.secondary)
                                .font(.caption)
                            TextField("Search in dump...", text: $searchText)
                                .font(.subheadline)
                                .textFieldStyle(.plain)
                                .autocorrectionDisabled()
                                .textInputAutocapitalization(.never)
                            if !searchText.isEmpty {
                                Button {
                                    searchText = ""
                                } label: {
                                    Image(systemName: "xmark.circle.fill")
                                        .foregroundColor(.secondary)
                                        .font(.caption)
                                }
                                .accessibilityLabel("Clear search")
                            }
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 8)
                        .background(.ultraThinMaterial)
                    }
                }
                .onChange(of: searchText) { newValue in
                    debounceTask?.cancel()
                    if newValue.isEmpty {
                        highlightedIndices = []
                        matchCount = 0
                        return
                    }
                    debounceTask = Task { @MainActor in
                        try? await Task.sleep(nanoseconds: 300_000_000)
                        guard !Task.isCancelled else { return }
                        performSearch(query: newValue)
                    }
                }
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
                    ToolbarItem(placement: .secondaryAction) {
                        Button {
                            UIPasteboard.general.string = dumpText
                            let generator = UIImpactFeedbackGenerator(style: .medium)
                            generator.impactOccurred()
                            showCopied = true
                            DispatchQueue.main.asyncAfter(deadline: .now() + 1.5) {
                                showCopied = false
                            }
                        } label: {
                            Label("Copy All", systemImage: "doc.on.doc")
                        }
                        .accessibilityLabel("Copy to clipboard")
                    }
                    ToolbarItem(placement: .secondaryAction) {
                        Menu {
                            ForEach(DumpFontSize.allCases) { size in
                                Button {
                                    fontSizeRaw = size.rawValue
                                } label: {
                                    if size.rawValue == fontSizeRaw {
                                        Label(size.rawValue, systemImage: "checkmark")
                                    } else {
                                        Text(size.rawValue)
                                    }
                                }
                            }
                        } label: {
                            Label("Font Size", systemImage: "textformat.size")
                        }
                        .accessibilityLabel("Change font size")
                    }
                }
                .safeAreaInset(edge: .bottom) {
                    if matchCount > 0 {
                        Text("\(matchCount) matches")
                            .font(.caption)
                            .foregroundColor(.secondary)
                            .padding(.horizontal, 12)
                            .padding(.vertical, 6)
                            .background(.ultraThinMaterial)
                            .clipShape(Capsule())
                            .padding(.bottom, 8)
                    }
                }
            }
        }
    }

    private func performSearch(query: String) {
        let lower = query.lowercased()
        var indices = Set<Int>()
        for (index, line) in cachedLines.enumerated() {
            if line.lowercased().contains(lower) {
                indices.insert(index)
            }
        }
        highlightedIndices = indices
        matchCount = indices.count
    }
}

// MARK: - DumpLineView

struct DumpLineView: View {
    let lineNumber: Int
    let text: String
    let isHighlighted: Bool
    var fontSize: DumpFontSize = .medium

    var body: some View {
        HStack(alignment: .top, spacing: 0) {
            // Line number
            Text("\(lineNumber)")
                .font(fontSize.lineNumberFont)
                .foregroundColor(Color.secondary.opacity(0.5))
                .frame(minWidth: 40, alignment: .trailing)
                .padding(.trailing, 8)

            // Syntax-highlighted line
            Text(highlightedText)
                .font(fontSize.lineFont)
                .lineLimit(1)
                .fixedSize(horizontal: true, vertical: false)
                .textSelection(.enabled)
        }
        .padding(.vertical, 0.5)
        .background(isHighlighted ? Color.yellow.opacity(0.2) : Color.clear)
    }

    private var highlightedText: AttributedString {
        var attr = AttributedString(text)
        let str = text.trimmingCharacters(in: .whitespaces)

        // Keywords in blue
        let keywords = [
            "@interface", "@end", "@protocol", "@property",
            "@optional", "@required", "@class",
            "@private", "@protected", "@public", "@package"
        ]
        for keyword in keywords {
            if let range = attr.range(of: keyword) {
                attr[range].foregroundColor = .systemBlue
                attr[range].font = fontSize.lineFont.bold()
            }
        }

        // Comments in green (lines starting with // or /*)
        if str.hasPrefix("//") || str.hasPrefix("/*") || str.hasPrefix(" *") || str.hasPrefix("*/") {
            attr.foregroundColor = .systemGreen
        }

        // Method signatures: - and + at start
        if str.hasPrefix("- (") || str.hasPrefix("+ (") {
            let marker: String = str.hasPrefix("-") ? "-" : "+"
            if let range = attr.range(of: marker) {
                attr[range].foregroundColor = .systemPurple
                attr[range].font = fontSize.lineFont.bold()
            }
        }

        // String literals starting with @"
        if let range = attr.range(of: "@\"") {
            attr[range].foregroundColor = .systemRed
        }

        return attr
    }
}
