import Foundation
import UniformTypeIdentifiers

final class FileImportService: Sendable {
    /// Content types the app can import
    static let supportedTypes: [UTType] = [
        .data,
        UTType(filenameExtension: "dylib") ?? .data,
        UTType(filenameExtension: "framework") ?? .data,
    ]

    private static let maxFileSize = 2 * 1_073_741_824 // 2 GB

    /// Validate that the data starts with a recognized Mach-O or FAT magic number
    nonisolated static func validateMachOMagic(_ data: Data) -> Bool {
        guard data.count >= 4 else { return false }
        let magic = data.withUnsafeBytes { $0.load(as: UInt32.self) }
        let validMagics: Set<UInt32> = [
            0xFEEDFACE, 0xCEFAEDFE,  // 32-bit
            0xFEEDFACF, 0xCFFAEDFE,  // 64-bit
            0xCAFEBABE, 0xBEBAFECA   // FAT
        ]
        return validMagics.contains(magic)
    }

    /// Read file data from a security-scoped URL
    nonisolated static func readFileData(from url: URL) throws -> Data {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }

        guard FileManager.default.isReadableFile(atPath: url.path) else {
            // Distinguish between security-scope failure and general access denial.
            // When startAccessingSecurityScopedResource() returns false AND the file
            // is not readable, the sandbox could not grant access.
            if !accessing {
                throw AnalysisError.securityScopeAccessDenied
            }
            throw AnalysisError.fileAccessDenied
        }

        do {
            let data = try Data(contentsOf: url, options: .mappedIfSafe)
            if data.count > maxFileSize {
                throw AnalysisError.fileTooLarge(data.count)
            }
            guard validateMachOMagic(data) else {
                throw AnalysisError.invalidBinary("File is not a Mach-O binary")
            }
            return data
        } catch let error as AnalysisError {
            throw error
        } catch {
            throw AnalysisError.fileReadFailed
        }
    }

    /// Create a bookmark for the file URL for later access (recent files)
    nonisolated static func createBookmark(for url: URL) -> Data? {
        let accessing = url.startAccessingSecurityScopedResource()
        defer { if accessing { url.stopAccessingSecurityScopedResource() } }
        return try? url.bookmarkData(options: .minimalBookmark, includingResourceValuesForKeys: nil, relativeTo: nil)
    }

    /// Resolve a bookmark back to a URL
    /// Returns the resolved URL and whether the bookmark is stale (caller should recreate bookmark if stale)
    nonisolated static func resolveBookmark(_ data: Data) -> (url: URL, isStale: Bool)? {
        var stale = false
        guard let url = try? URL(resolvingBookmarkData: data, options: [], relativeTo: nil, bookmarkDataIsStale: &stale) else {
            return nil
        }
        return (url, stale)
    }
}
