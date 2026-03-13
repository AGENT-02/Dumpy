import XCTest
@testable import Dumpy

final class SafeReadTests: XCTestCase {
    func testCheckRangeValidRange() {
        XCTAssertTrue(safe_check_range(100, 0, 50))
        XCTAssertTrue(safe_check_range(100, 99, 1))
        XCTAssertTrue(safe_check_range(100, 0, 100))
    }

    func testCheckRangeOutOfBounds() {
        XCTAssertFalse(safe_check_range(100, 0, 101))
        XCTAssertFalse(safe_check_range(100, 100, 1))
        XCTAssertFalse(safe_check_range(100, 50, 51))
    }

    func testCheckRangeOverflow() {
        XCTAssertFalse(safe_check_range(100, Int.max, 1))
        XCTAssertFalse(safe_check_range(100, 1, Int.max))
    }

    func testReadUint32() {
        var data: [UInt8] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]
        var value: UInt32 = 0
        XCTAssertTrue(safe_read_uint32(&data, data.count, 0, &value))
        // Little-endian: 0x04030201
        XCTAssertEqual(value, 0x04030201)
    }

    func testReadUint32OutOfBounds() {
        var data: [UInt8] = [0x01, 0x02, 0x03]
        var value: UInt32 = 0
        XCTAssertFalse(safe_read_uint32(&data, data.count, 0, &value))
    }

    func testReadUint64() {
        var data: [UInt8] = [0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08]
        var value: UInt64 = 0
        XCTAssertTrue(safe_read_uint64(&data, data.count, 0, &value))
        XCTAssertEqual(value, 0x0807060504030201)
    }

    func testReadString() {
        var data: [UInt8] = Array("hello\0world\0".utf8)
        let str = safe_read_string(&data, data.count, 0, 100)
        XCTAssertNotNil(str)
        XCTAssertEqual(String(cString: str!), "hello")
    }

    func testReadStringNoNullTerminator() {
        var data: [UInt8] = [0x41, 0x42, 0x43] // "ABC" no null
        let str = safe_read_string(&data, data.count, 0, 3)
        XCTAssertNil(str)
    }

    func testReadStringAtOffset() {
        var data: [UInt8] = Array("hello\0world\0".utf8)
        let str = safe_read_string(&data, data.count, 6, 100)
        XCTAssertNotNil(str)
        XCTAssertEqual(String(cString: str!), "world")
    }

    func testReadBytesNullBuffer() {
        var value: UInt32 = 0
        XCTAssertFalse(safe_read_uint32(nil, 100, 0, &value))
    }
}
