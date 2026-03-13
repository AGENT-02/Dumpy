import XCTest
@testable import Dumpy

final class TypeEncodingTests: XCTestCase {

    private func decoded(_ encoding: String) -> String? {
        guard let result = decode_type_encoding(encoding) else { return nil }
        defer { free(result) }
        return String(cString: result)
    }

    func testPrimitiveChar() {
        XCTAssertEqual(decoded("c"), "char")
    }

    func testPrimitiveInt() {
        XCTAssertEqual(decoded("i"), "int")
    }

    func testPrimitiveFloat() {
        XCTAssertEqual(decoded("f"), "float")
    }

    func testPrimitiveDouble() {
        XCTAssertEqual(decoded("d"), "double")
    }

    func testPrimitiveBOOL() {
        XCTAssertEqual(decoded("B"), "BOOL")
    }

    func testPrimitiveVoid() {
        XCTAssertEqual(decoded("v"), "void")
    }

    func testPrimitiveLongLong() {
        XCTAssertEqual(decoded("q"), "long long")
    }

    func testPrimitiveId() {
        XCTAssertEqual(decoded("@"), "id")
    }

    func testPrimitiveClass() {
        XCTAssertEqual(decoded("#"), "Class")
    }

    func testPrimitiveSEL() {
        XCTAssertEqual(decoded(":"), "SEL")
    }

    func testPrimitiveCharPointer() {
        XCTAssertEqual(decoded("*"), "char *")
    }

    func testPrimitiveUnknown() {
        let result = decoded("?")
        XCTAssertNotNil(result)
        // Should contain some indication of unknown type
    }

    func testObjectWithClass() {
        let result = decoded("@\"NSString\"")
        XCTAssertNotNil(result)
        XCTAssertTrue(result!.contains("NSString"), "Expected NSString in '\(result!)'")
    }

    func testPointerToInt() {
        let result = decoded("^i")
        XCTAssertNotNil(result)
        XCTAssertTrue(result!.contains("int"), "Expected int in '\(result!)'")
        XCTAssertTrue(result!.contains("*"), "Expected pointer marker in '\(result!)'")
    }

    func testStruct() {
        let result = decoded("{CGRect={CGPoint=dd}{CGSize=dd}}")
        XCTAssertNotNil(result)
        XCTAssertTrue(result!.contains("CGRect"), "Expected CGRect in '\(result!)'")
    }

    func testArray() {
        let result = decoded("[10i]")
        XCTAssertNotNil(result)
        XCTAssertTrue(result!.contains("int"), "Expected int in '\(result!)'")
    }

    func testConstQualifier() {
        let result = decoded("r*")
        XCTAssertNotNil(result)
        XCTAssertTrue(result!.contains("const"), "Expected const in '\(result!)'")
        XCTAssertTrue(result!.contains("char"), "Expected char in '\(result!)'")
    }

    func testNullInput() {
        let result = decode_type_encoding(nil)
        XCTAssertNil(result)
    }

    func testEmptyString() {
        let result = decode_type_encoding("")
        // Should handle gracefully - either nil or some default
        if let r = result {
            free(r)
        }
    }
}
