import XCTest
@testable import Dumpy

final class SearchServiceTests: XCTestCase {

    private func makeEmptyResult() -> AnalysisResult {
        AnalysisResult(
            fileInfo: MachOFileInfo(fileName: "test.dylib", fileSize: 1000, isFat: false, architectures: []),
            header: MachOHeaderModel(
                magic: 0xFEEDFACF, cpuType: 0x0100000C, cpuSubtype: 0,
                fileType: 6, numberOfCommands: 0, sizeOfCommands: 0,
                flags: 0, is64Bit: true, archName: "arm64", fileTypeName: "dylib"
            ),
            loadCommands: [],
            segments: [],
            classes: [],
            protocols: [],
            categories: [],
            selectorCount: 0,
            dumpText: "",
            diagnostics: [],
            uuid: nil,
            minVersion: nil,
            sdkVersion: nil,
            sourceVersion: nil,
            selectedArchitecture: "arm64",
            linkedLibraries: [],
            rpaths: [],
            security: SecurityInfo(hasCodeSignature: false, codeSigOffset: 0, codeSigSize: 0, isEncrypted: false, cryptId: 0),
            symbols: [],
            signingStatus: nil,
            headerFlags: [],
            platform: nil,
            objcABIVersion: 0,
            swiftABIVersion: 0,
            swiftTypes: [],
            swiftDumpText: "",
            classHierarchy: [:],
            protocolConformers: [:],
            classCategoryMap: [:]
        )
    }

    private func makeResultWithClasses() -> AnalysisResult {
        let method1 = MethodModel(name: "viewDidLoad", typeEncoding: nil, returnType: "void", isClassMethod: false)
        let method2 = MethodModel(name: "initWithFrame:", typeEncoding: nil, returnType: "id", isClassMethod: false)
        let property1 = PropertyModel(name: "title", attributes: nil, typeName: "NSString", isReadonly: false, isNonatomic: true, isWeak: false, isCopy: true, isRetain: false)

        let cls1 = ClassModel(
            name: "MyViewController",
            superclassName: "UIViewController",
            isSwiftClass: false,
            instanceSize: 64,
            instanceMethods: [method1, method2],
            classMethods: [],
            ivars: [],
            properties: [property1],
            protocols: ["UITableViewDelegate"]
        )

        let cls2 = ClassModel(
            name: "AppDelegate",
            superclassName: "UIResponder",
            isSwiftClass: false,
            instanceSize: 32,
            instanceMethods: [],
            classMethods: [],
            ivars: [],
            properties: [],
            protocols: []
        )

        let proto1 = ProtocolModel(
            name: "NetworkService",
            instanceMethods: [MethodModel(name: "fetchData", typeEncoding: nil, returnType: "void", isClassMethod: false)],
            classMethods: [],
            optionalInstanceMethods: [],
            optionalClassMethods: [],
            properties: [],
            adoptedProtocols: []
        )

        var result = makeEmptyResult()
        // We need to create a new AnalysisResult with classes
        return AnalysisResult(
            fileInfo: result.fileInfo,
            header: result.header,
            loadCommands: result.loadCommands,
            segments: result.segments,
            classes: [cls1, cls2],
            protocols: [proto1],
            categories: [],
            selectorCount: result.selectorCount,
            dumpText: result.dumpText,
            diagnostics: result.diagnostics,
            uuid: result.uuid,
            minVersion: result.minVersion,
            sdkVersion: result.sdkVersion,
            sourceVersion: result.sourceVersion,
            selectedArchitecture: result.selectedArchitecture,
            linkedLibraries: result.linkedLibraries,
            rpaths: result.rpaths,
            security: result.security,
            symbols: [],
            signingStatus: nil,
            headerFlags: [],
            platform: nil,
            objcABIVersion: 0,
            swiftABIVersion: 0,
            swiftTypes: [],
            swiftDumpText: "",
            classHierarchy: [:],
            protocolConformers: [:],
            classCategoryMap: [:]
        )
    }

    func testEmptyQueryReturnsEmpty() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "", in: result)
        XCTAssertTrue(search.isEmpty)
    }

    func testExactClassNameMatch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "MyViewController", in: result)
        XCTAssertEqual(search.classes.count, 1)
        XCTAssertEqual(search.classes.first?.name, "MyViewController")
    }

    func testCaseInsensitiveMatch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "myviewcontroller", in: result)
        XCTAssertEqual(search.classes.count, 1)
        XCTAssertEqual(search.classes.first?.name, "MyViewController")
    }

    func testMethodNameSearch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "viewDidLoad", in: result)
        XCTAssertEqual(search.methods.count, 1)
        XCTAssertEqual(search.methods.first?.method.name, "viewDidLoad")
        XCTAssertEqual(search.methods.first?.ownerName, "MyViewController")
    }

    func testNoResultsForNonMatchingQuery() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "zzzzNotExisting", in: result)
        XCTAssertTrue(search.isEmpty)
        XCTAssertEqual(search.totalCount, 0)
    }

    func testSearchWithEmptyAnalysisResult() {
        let result = makeEmptyResult()
        let search = SearchService.search(query: "anything", in: result)
        XCTAssertTrue(search.isEmpty)
    }

    func testPartialClassNameMatch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "ViewController", in: result)
        XCTAssertEqual(search.classes.count, 1)
    }

    func testProtocolSearch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "NetworkService", in: result)
        XCTAssertEqual(search.protocols.count, 1)
        XCTAssertEqual(search.protocols.first?.name, "NetworkService")
    }

    func testPropertySearch() {
        let result = makeResultWithClasses()
        let search = SearchService.search(query: "title", in: result)
        XCTAssertEqual(search.properties.count, 1)
        XCTAssertEqual(search.properties.first?.property.name, "title")
    }
}
