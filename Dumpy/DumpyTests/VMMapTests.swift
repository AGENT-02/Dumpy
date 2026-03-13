import XCTest
@testable import Dumpy

final class VMMapTests: XCTestCase {

    func testStripPacPointer() {
        // A PAC-tagged pointer: top bits set
        let tagged: UInt64 = 0x8000_0001_0000_1234
        let stripped = strip_pac_pointer(tagged, true)
        // Should strip top 16 bits, keeping lower 48
        XCTAssertEqual(stripped, 0x0000_0001_0000_1234,
                       "Expected PAC bits stripped: got 0x\(String(stripped, radix: 16))")
    }

    func testStripPacPointerNoTag() {
        // A clean pointer should remain unchanged
        let clean: UInt64 = 0x0000_0001_0000_5678
        let stripped = strip_pac_pointer(clean, true)
        XCTAssertEqual(stripped, clean)
    }

    func testStripPacPointer32Bit() {
        // For 32-bit, stripping shouldn't alter valid 32-bit addresses
        let ptr: UInt64 = 0x0000_0000_1234_5678
        let stripped = strip_pac_pointer(ptr, false)
        XCTAssertEqual(stripped & 0xFFFFFFFF, 0x1234_5678)
    }

    func testVMMapBuildAndLookup() {
        // Create a minimal SectionsInfo with one segment
        var segment = SegmentInfo()
        let segname = "__TEXT"
        withUnsafeMutablePointer(to: &segment.segname) { ptr in
            ptr.withMemoryRebound(to: CChar.self, capacity: 17) { buf in
                segname.withCString { src in
                    _ = strcpy(buf, src)
                }
            }
        }
        segment.vmaddr = 0x100000000
        segment.vmsize = 0x1000
        segment.fileoff = 0
        segment.filesize = 0x1000
        segment.maxprot = 5 // r-x
        segment.initprot = 5
        segment.nsects = 0
        segment.sections = nil

        var sectionsInfo = SectionsInfo()
        let segPtr = UnsafeMutablePointer<SegmentInfo>.allocate(capacity: 1)
        segPtr.initialize(to: segment)
        sectionsInfo.segments = segPtr
        sectionsInfo.segment_count = 1
        defer { segPtr.deallocate() }

        var vmmap = VMMap()
        let diags = diag_list_create()!
        defer {
            diag_list_destroy(diags)
            vmmap_destroy(&vmmap)
        }

        let result = vmmap_build(&sectionsInfo, &vmmap, diags)
        XCTAssertEqual(result, DIAG_OK)
        XCTAssertGreaterThan(vmmap.count, 0)

        // Test address within mapped region
        var fileOffset: Int = 0
        let found = vmmap_to_file_offset(&vmmap, 0x100000100, &fileOffset)
        XCTAssertTrue(found)
        XCTAssertEqual(fileOffset, 0x100)

        // Test address outside mapped region
        var outOffset: Int = 0
        let notFound = vmmap_to_file_offset(&vmmap, 0x200000000, &outOffset)
        XCTAssertFalse(notFound)
    }

    func testVMMapEmptySections() {
        var sectionsInfo = SectionsInfo()
        sectionsInfo.segments = nil
        sectionsInfo.segment_count = 0

        var vmmap = VMMap()
        let diags = diag_list_create()!
        defer {
            diag_list_destroy(diags)
            vmmap_destroy(&vmmap)
        }

        let result = vmmap_build(&sectionsInfo, &vmmap, diags)
        // Should handle gracefully even with no segments
        XCTAssertEqual(result, DIAG_OK)
        XCTAssertEqual(vmmap.count, 0)
    }
}
