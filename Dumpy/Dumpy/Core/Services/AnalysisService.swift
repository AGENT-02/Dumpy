import Foundation
import Combine

enum AnalysisPhase: Sendable {
    case idle
    case reading
    case parsingFatHeader
    case selectingArchitecture
    case analyzing
    case complete
    case failed(AnalysisError)
}

@MainActor
final class AnalysisService: ObservableObject {
    @Published var phase: AnalysisPhase = .idle
    @Published var progress: String = ""
    @Published var fileInfo: MachOFileInfo?
    @Published var selectedArchIndex: Int = 0
    @Published var result: AnalysisResult?

    private var fileData: Data?
    private var fatResult: FatParseResult?
    private var analysisTask: Task<Void, Never>?

    func cancel() {
        analysisTask?.cancel()
        analysisTask = nil
        phase = .idle
        progress = ""
    }

    func importFile(url: URL) {
        phase = .reading
        progress = "Reading file..."

        analysisTask = Task(priority: .userInitiated) { [weak self] in
            do {
                let data = try await Task.detached(priority: .userInitiated) {
                    try FileImportService.readFileData(from: url)
                }.value
                let fileName = url.lastPathComponent

                guard let self else { return }
                self.fileData = data
                self.progress = "Parsing FAT header..."
                self.phase = .parsingFatHeader

                let fatResult = try await Task.detached(priority: .userInitiated) {
                    try MachOAnalyzerBridge.parseFatHeader(data: data)
                }.value

                let fileInfo = MachOFileInfo(
                    fileName: fileName,
                    fileSize: UInt64(data.count),
                    isFat: fatResult.isFat,
                    architectures: fatResult.architectures
                )

                self.fatResult = fatResult
                self.fileInfo = fileInfo

                if fatResult.architectures.count > 1 {
                    self.phase = .selectingArchitecture
                } else {
                    self.beginAnalysis(archIndex: 0)
                }
            } catch let error as AnalysisError {
                guard let self else { return }
                self.phase = .failed(error)
            } catch is CancellationError {
                // Task was cancelled; no state update needed
            } catch {
                guard let self else { return }
                self.phase = .failed(.fileImportFailed(error.localizedDescription))
            }
        }
    }

    func selectArchitecture(_ index: Int) {
        selectedArchIndex = index
        beginAnalysis(archIndex: index)
    }

    func reset() {
        cancel()
        phase = .idle
        progress = ""
        fileInfo = nil
        selectedArchIndex = 0
        result = nil
        fileData = nil
        fatResult = nil
    }

    private func beginAnalysis(archIndex: Int) {
        guard let data = fileData, let fatResult = fatResult, let fileInfo = fileInfo else { return }

        guard archIndex >= 0 && archIndex < fatResult.architectures.count else {
            phase = .failed(.parseFailed("Invalid architecture index"))
            return
        }

        phase = .analyzing
        progress = "Analyzing binary..."

        let arch = fatResult.architectures[archIndex]
        let fileName = fileInfo.fileName

        analysisTask = Task(priority: .userInitiated) { [weak self] in
            do {
                let result = try await withThrowingTaskGroup(of: AnalysisResult.self) { group in
                    group.addTask {
                        try await Task.detached(priority: .userInitiated) {
                            try MachOAnalyzerBridge.analyze(
                                data: data,
                                archOffset: arch.offset,
                                archSize: arch.size,
                                fileName: fileName,
                                onProgress: { message in
                                    Task { @MainActor [weak self] in
                                        self?.progress = message
                                    }
                                }
                            )
                        }.value
                    }
                    group.addTask {
                        try await Task.sleep(nanoseconds: 60_000_000_000)
                        throw AnalysisError.analysisTimedOut
                    }
                    guard let first = try await group.next() else {
                        throw AnalysisError.analysisTimedOut
                    }
                    group.cancelAll()
                    return first
                }
                guard let self else { return }
                self.result = result
                self.fileData = nil  // Release binary data after successful analysis
                self.phase = .complete
                self.progress = "Analysis complete"
            } catch let error as AnalysisError {
                guard let self else { return }
                self.phase = .failed(error)
            } catch is CancellationError {
                // Task was cancelled; no state update needed
            } catch {
                guard let self else { return }
                self.phase = .failed(.analysisFailedGeneric(error.localizedDescription))
            }
        }
    }
}
