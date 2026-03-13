import XCTest
@testable import Dumpy

final class DiagnosticsTests: XCTestCase {
    func testCreateAndDestroy() {
        let list = diag_list_create()
        XCTAssertNotNil(list)
        XCTAssertEqual(list!.pointee.count, 0)
        diag_list_destroy(list)
    }

    func testAddEntry() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        diag_add(list, DIAG_ERR_INVALID_MAGIC, 0, "bad magic")
        XCTAssertEqual(list.pointee.count, 1)
        XCTAssertEqual(list.pointee.entries[0].code, DIAG_ERR_INVALID_MAGIC)
        XCTAssertEqual(list.pointee.entries[0].offset, 0)
    }

    func testHasErrors() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        XCTAssertFalse(diag_has_errors(list))
        diag_add(list, DIAG_ERR_TRUNCATED, 10, "truncated")
        XCTAssertTrue(diag_has_errors(list))
    }

    func testHasWarnings() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        XCTAssertFalse(diag_has_warnings(list))
        diag_add(list, DIAG_WARN_PARTIAL_METADATA, 0, "partial")
        XCTAssertTrue(diag_has_warnings(list))
        // Warnings are not errors
        XCTAssertFalse(diag_has_errors(list))
    }

    func testHasErrorsNotTriggeredByWarnings() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        diag_add(list, DIAG_WARN_ALIGNMENT, 0, "align warn")
        XCTAssertFalse(diag_has_errors(list))
        XCTAssertTrue(diag_has_warnings(list))
    }

    func testAddMultipleEntries() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        diag_add(list, DIAG_ERR_INVALID_MAGIC, 0, "error1")
        diag_add(list, DIAG_WARN_PARTIAL_METADATA, 100, "warning1")
        diag_add(list, DIAG_ERR_TRUNCATED, 200, "error2")

        XCTAssertEqual(list.pointee.count, 3)
        XCTAssertTrue(diag_has_errors(list))
        XCTAssertTrue(diag_has_warnings(list))
    }

    func testCapacityGrowth() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        // Add enough entries to force capacity growth
        for i in 0..<50 {
            diag_add(list, DIAG_ERR_INVALID_OFFSET, i, "entry")
        }
        XCTAssertEqual(list.pointee.count, 50)
        XCTAssertGreaterThanOrEqual(list.pointee.capacity, 50)
    }

    func testAddWithMessage() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        diag_add(list, DIAG_ERR_INVALID_OFFSET, 42, "offset is invalid")
        XCTAssertEqual(list.pointee.count, 1)

        let msg = withUnsafePointer(to: list.pointee.entries[0].message) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: 256) { String(cString: $0) }
        }
        XCTAssertTrue(msg.contains("offset is invalid"), "Expected message text: \(msg)")
    }

    func testEntryOffset() {
        let list = diag_list_create()!
        defer { diag_list_destroy(list) }

        diag_add(list, DIAG_ERR_INVALID_SECTION, 12345, "bad section")
        XCTAssertEqual(list.pointee.entries[0].offset, 12345)
    }
}
