import Combine
import SwiftUI

@main
struct DumpyApp: App {
    @StateObject private var openedFileState = OpenedFileState()

    var body: some Scene {
        WindowGroup {
            HomeView(openedFileState: openedFileState)
                .onAppear {
                    ExportService.cleanupTempFiles()
                }
                .onOpenURL { url in
                    openedFileState.pendingURL = url
                }
        }
    }
}

final class OpenedFileState: ObservableObject {
    @Published var pendingURL: URL?
}
