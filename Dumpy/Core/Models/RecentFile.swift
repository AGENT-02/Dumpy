import Foundation
import Combine

struct RecentFile: Codable, Identifiable, Sendable {
    var id: String { fileName + String(lastOpened.timeIntervalSince1970) }
    var fileName: String
    var bookmarkData: Data
    var lastOpened: Date
    var fileSize: Int64
    var architectureSummary: String
}

@MainActor
final class RecentFilesStore: ObservableObject {
    @Published var files: [RecentFile] = []

    private static let key = "com.dumpy.recentFiles"

    init() {
        load()
    }

    func load() {
        guard let data = UserDefaults.standard.data(forKey: Self.key),
              let decoded = try? JSONDecoder().decode([RecentFile].self, from: data) else {
            return
        }
        files = decoded.sorted { $0.lastOpened > $1.lastOpened }
    }

    func save() {
        guard let data = try? JSONEncoder().encode(files) else { return }
        UserDefaults.standard.set(data, forKey: Self.key)
    }

    func addOrUpdate(fileName: String, bookmarkData: Data, fileSize: Int64, architectureSummary: String) {
        files.removeAll { $0.fileName == fileName }
        let entry = RecentFile(
            fileName: fileName,
            bookmarkData: bookmarkData,
            lastOpened: Date(),
            fileSize: fileSize,
            architectureSummary: architectureSummary
        )
        files.insert(entry, at: 0)
        if files.count > 20 { files = Array(files.prefix(20)) }
        save()
    }

    func remove(at offsets: IndexSet) {
        for index in offsets.sorted().reversed() {
            files.remove(at: index)
        }
        save()
    }

    func clearAll() {
        files.removeAll()
        save()
    }
}
