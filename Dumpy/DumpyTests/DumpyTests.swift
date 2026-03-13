import XCTest
@testable import Dumpy

final class DumpyTests: XCTestCase {

    /// Helper to write a UInt32 in little-endian at a given offset
    private func writeUInt32(_ data: inout [UInt8], offset: Int, value: UInt32) {
        data[offset + 0] = UInt8(value & 0xFF)
        data[offset + 1] = UInt8((value >> 8) & 0xFF)
        data[offset + 2] = UInt8((value >> 16) & 0xFF)
        data[offset + 3] = UInt8((value >> 24) & 0xFF)
    }

    func testMachOAnalyzerBridgeWithInvalidData() {
        // Random bytes that are not a valid Mach-O
        let data = Data(repeating: 0x00, count: 64)
        XCTAssertThrowsError(try MachOAnalyzerBridge.parseFatHeader(data: data)) { error in
            // Should get an invalidMachO or similar error
            XCTAssertTrue(error is AnalysisError, "Expected AnalysisError, got \(type(of: error))")
        }
    }

    func testMachOAnalyzerBridgeWithMinimalHeader() {
        // Build a minimal 64-bit Mach-O header
        var data = [UInt8](repeating: 0, count: 32)
        writeUInt32(&data, offset: 0, value: 0xFEEDFACF)        // magic (64-bit)
        writeUInt32(&data, offset: 4, value: 0x0100000C)         // cputype: ARM64
        writeUInt32(&data, offset: 8, value: 0)                  // cpusubtype
        writeUInt32(&data, offset: 12, value: 2)                 // filetype: MH_EXECUTE
        writeUInt32(&data, offset: 16, value: 0)                 // ncmds
        writeUInt32(&data, offset: 20, value: 0)                 // sizeofcmds
        writeUInt32(&data, offset: 24, value: 0)                 // flags
        writeUInt32(&data, offset: 28, value: 0)                 // reserved

        let swiftData = Data(data)
        do {
            let result = try MachOAnalyzerBridge.parseFatHeader(data: swiftData)
            XCTAssertFalse(result.isFat)
            XCTAssertEqual(result.architectures.count, 1)
            XCTAssertEqual(result.architectures.first?.name, "arm64")
        } catch {
            XCTFail("Should not throw for valid minimal header: \(error)")
        }
    }

    func testMachOAnalyzerBridgeWithEmptyData() {
        let data = Data()
        XCTAssertThrowsError(try MachOAnalyzerBridge.parseFatHeader(data: data))
    }

    func testAnalysisErrorDescriptions() {
        // Verify error descriptions are non-nil
        let errors: [AnalysisError] = [
            .fileAccessDenied,
            .fileReadFailed,
            .invalidMachO("test"),
            .truncatedFile,
            .unsupportedArchitecture,
            .parseFailed("test"),
            .noObjCMetadata,
            .allocationFailed,
            .fileTooLarge(1_000_000_000),
        ]
        for err in errors {
            XCTAssertNotNil(err.errorDescription, "Error \(err) should have a description")
        }
    }
}
