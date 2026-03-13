import XCTest
@testable import Dumpy

final class MachOReaderTests: XCTestCase {

    /// Helper to build a minimal 64-bit Mach-O header (MachOHeader64 = 32 bytes)
    private func makeHeader64(magic: UInt32 = 0xFEEDFACF,
                              cputype: UInt32 = 0x0100000C, // ARM64
                              cpusubtype: UInt32 = 0,
                              filetype: UInt32 = 2, // MH_EXECUTE
                              ncmds: UInt32 = 0,
                              sizeofcmds: UInt32 = 0,
                              flags: UInt32 = 0,
                              reserved: UInt32 = 0) -> [UInt8] {
        var data = [UInt8](repeating: 0, count: 32)
        withUnsafeMutableBytes(of: &data) { _ in }
        // Write fields as little-endian
        writeUInt32(&data, offset: 0, value: magic)
        writeUInt32(&data, offset: 4, value: cputype)
        writeUInt32(&data, offset: 8, value: cpusubtype)
        writeUInt32(&data, offset: 12, value: filetype)
        writeUInt32(&data, offset: 16, value: ncmds)
        writeUInt32(&data, offset: 20, value: sizeofcmds)
        writeUInt32(&data, offset: 24, value: flags)
        writeUInt32(&data, offset: 28, value: reserved)
        return data
    }

    /// Helper to build a minimal 32-bit Mach-O header (MachOHeader32 = 28 bytes)
    private func makeHeader32(magic: UInt32 = 0xFEEDFACE,
                              cputype: UInt32 = 12, // ARM
                              cpusubtype: UInt32 = 0,
                              filetype: UInt32 = 2,
                              ncmds: UInt32 = 0,
                              sizeofcmds: UInt32 = 0,
                              flags: UInt32 = 0) -> [UInt8] {
        var data = [UInt8](repeating: 0, count: 28)
        writeUInt32(&data, offset: 0, value: magic)
        writeUInt32(&data, offset: 4, value: cputype)
        writeUInt32(&data, offset: 8, value: cpusubtype)
        writeUInt32(&data, offset: 12, value: filetype)
        writeUInt32(&data, offset: 16, value: ncmds)
        writeUInt32(&data, offset: 20, value: sizeofcmds)
        writeUInt32(&data, offset: 24, value: flags)
        return data
    }

    private func writeUInt32(_ data: inout [UInt8], offset: Int, value: UInt32) {
        data[offset + 0] = UInt8(value & 0xFF)
        data[offset + 1] = UInt8((value >> 8) & 0xFF)
        data[offset + 2] = UInt8((value >> 16) & 0xFF)
        data[offset + 3] = UInt8((value >> 24) & 0xFF)
    }

    func testContextInit64BitMagic() {
        var data = makeHeader64()
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        let result = macho_context_init(&ctx, &data, data.count, diags)
        XCTAssertEqual(result, DIAG_OK)
        XCTAssertTrue(ctx.is_64bit)
        XCTAssertFalse(ctx.needs_swap)
        XCTAssertEqual(ctx.magic, 0xFEEDFACF)
    }

    func testContextInit32BitMagic() {
        var data = makeHeader32()
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        let result = macho_context_init(&ctx, &data, data.count, diags)
        XCTAssertEqual(result, DIAG_OK)
        XCTAssertFalse(ctx.is_64bit)
        XCTAssertFalse(ctx.needs_swap)
        XCTAssertEqual(ctx.magic, 0xFEEDFACE)
    }

    func testContextInitInvalidMagic() {
        var data: [UInt8] = [0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x00]
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        let result = macho_context_init(&ctx, &data, data.count, diags)
        XCTAssertNotEqual(result, DIAG_OK)
    }

    func testContextInitTruncated() {
        // Only 4 bytes - too small for any header
        var data: [UInt8] = [0xCF, 0xFA, 0xED, 0xFE]
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        let result = macho_context_init(&ctx, &data, data.count, diags)
        XCTAssertNotEqual(result, DIAG_OK)
    }

    func testContextInitCigam64() {
        // CIGAM_64 = 0xCFFAEDFE -> byte-swapped 64-bit
        var data = makeHeader64(magic: 0xCFFAEDFE)
        // When magic is CIGAM, the CPU type etc. are also swapped, but context_init
        // should still recognize the magic and set needs_swap.
        // Write magic as big-endian: CF FA ED FE
        data[0] = 0xCF
        data[1] = 0xFA
        data[2] = 0xED
        data[3] = 0xFE
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        let result = macho_context_init(&ctx, &data, data.count, diags)
        XCTAssertEqual(result, DIAG_OK)
        XCTAssertTrue(ctx.needs_swap)
        XCTAssertTrue(ctx.is_64bit)
    }

    func testSwap32NoSwap() {
        var data = makeHeader64()
        var ctx = MachOContext()
        let diags = diag_list_create()!
        defer { diag_list_destroy(diags) }

        _ = macho_context_init(&ctx, &data, data.count, diags)
        // needs_swap is false, so swap32 should return value unchanged
        XCTAssertEqual(macho_swap32(&ctx, 0x12345678), 0x12345678)
    }
}
