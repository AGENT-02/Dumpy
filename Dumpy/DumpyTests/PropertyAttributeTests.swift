import XCTest
@testable import Dumpy

final class PropertyAttributeTests: XCTestCase {

    private func parsedProperty(_ attrs: String) -> ObjCProperty {
        var prop = ObjCProperty()
        attrs.withCString { cstr in
            parse_property_attributes(cstr, &prop)
        }
        return prop
    }

    private func cleanupProperty(_ prop: inout ObjCProperty) {
        objc_property_destroy(&prop)
    }

    func testNSStringCopyNonatomic() {
        var prop = parsedProperty("T@\"NSString\",C,N,V_name")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_copy)
        XCTAssertTrue(prop.is_nonatomic)
        XCTAssertFalse(prop.is_readonly)
        XCTAssertFalse(prop.is_weak)
        XCTAssertFalse(prop.is_retain)

        if let typeName = prop.type_name {
            let typeStr = String(cString: typeName)
            XCTAssertTrue(typeStr.contains("NSString"), "Expected NSString in type: \(typeStr)")
        }
    }

    func testIntNonatomic() {
        var prop = parsedProperty("Ti,N,V_count")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_nonatomic)
        XCTAssertFalse(prop.is_copy)
        XCTAssertFalse(prop.is_readonly)
    }

    func testReadonlyNonatomic() {
        var prop = parsedProperty("T@\"NSArray\",R,N")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_readonly)
        XCTAssertTrue(prop.is_nonatomic)
    }

    func testRetainNonatomic() {
        var prop = parsedProperty("T@,&,N")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_retain)
        XCTAssertTrue(prop.is_nonatomic)
    }

    func testCustomGetter() {
        var prop = parsedProperty("TB,N,GisEnabled,V_enabled")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_nonatomic)
        if let getter = prop.getter {
            XCTAssertEqual(String(cString: getter), "isEnabled")
        } else {
            XCTFail("Expected getter to be set")
        }
    }

    func testWeakProperty() {
        var prop = parsedProperty("T@\"NSString\",W,N")
        defer { cleanupProperty(&prop) }

        XCTAssertTrue(prop.is_weak)
        XCTAssertTrue(prop.is_nonatomic)
    }
}
