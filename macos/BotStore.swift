import AppKit
import Observation

@MainActor
@Observable
final class BotStore {
    var devices: [DeviceSnapshot] = []
    var logs: [LogLine] = []
    var discovering = false
    var shuttingDown = false
    var message = "准备连接"
    var error = ""
    var logPath = ""
    var endpoint = "127.0.0.1:5555"
    var selectedSerial: String?
    var logQuery = ""
    var errorsOnly = false
    var followLogs = true
    var operationError: String?

    @ObservationIgnored private var backend: OpaquePointer?
    @ObservationIgnored private var started = false

    var selectedDevice: DeviceSnapshot? { devices.first { $0.serial == selectedSerial } }
    var runningCount: Int { devices.filter(\.running).count }
    var waitingCount: Int { devices.filter(\.awaitingInput).count }
    var visibleLogs: [LogLine] {
        logs.filter { (!errorsOnly || $0.isError) && (logQuery.isEmpty || $0.text.localizedCaseInsensitiveContains(logQuery)) }
    }

    func start() {
        guard !started else { return }
        started = true
        do {
            let manager = FileManager.default
            let directory: URL
            // An override keeps automated smoke tests out of the user's logs.
            if let path = ProcessInfo.processInfo.environment["CRBOT_DATA_DIR"] {
                directory = URL(fileURLWithPath: path, isDirectory: true)
            } else {
                directory = manager.homeDirectoryForCurrentUser
                    .appendingPathComponent("Library/Logs/CRBot", isDirectory: true)
            }
            try manager.createDirectory(at: directory, withIntermediateDirectories: true)
            // Debug screenshots use relative paths in the shared C++ core.
            guard manager.changeCurrentDirectoryPath(directory.path) else {
                throw CocoaError(.fileWriteNoPermission)
            }
            backend = crbot_create(directory.path)
            guard backend != nil else { throw CocoaError(.fileWriteUnknown) }
            scan()
        } catch {
            self.error = "无法初始化 CRBot：\(error.localizedDescription)"
        }
    }

    func observe() async {
        start()
        while !Task.isCancelled {
            refresh()
            try? await Task.sleep(for: .milliseconds(300))
        }
    }

    func refresh() {
        guard let backend, let pointer = crbot_snapshot(backend) else { return }
        defer { crbot_free_string(pointer) }
        do {
            let snapshot = try JSONDecoder().decode(BackendSnapshot.self, from: Data(String(cString: pointer).utf8))
            if devices != snapshot.devices { devices = snapshot.devices }
            if logs != snapshot.logs { logs = snapshot.logs }
            discovering = snapshot.discovering
            logPath = snapshot.logPath
            message = snapshot.message
            error = snapshot.error
            if selectedSerial == nil { selectedSerial = devices.first?.serial }
        } catch {
            self.error = "读取运行状态失败：\(error.localizedDescription)"
        }
    }

    func scan() {
        guard let backend, !shuttingDown, !discovering else { return }
        crbot_discover(backend, "")
        refresh()
    }

    func connect() {
        guard let backend, !shuttingDown, !discovering else { return }
        let target = endpoint.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !target.isEmpty else { return }
        crbot_discover(backend, target)
        refresh()
    }

    func cancelScan() { crbot_cancel_discovery(backend) }

    func perform(_ action: DeviceAction, on device: DeviceSnapshot) {
        guard !shuttingDown else { return }
        crbot_device_action(backend, device.serial, action.rawValue)
        refresh()
    }

    func stopAll() {
        crbot_stop_all(backend)
        refresh()
    }

    func clearLogs() {
        crbot_clear_logs(backend)
        refresh()
    }

    func revealLogs() {
        guard !logPath.isEmpty else { return }
        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: logPath)])
    }

    func copyLogs() {
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(visibleLogs.map(\.text).joined(separator: "\n"), forType: .string)
    }

    func exportLogs() {
        let panel = NSSavePanel()
        panel.nameFieldStringValue = "crbot.log"
        panel.title = "导出完整日志"
        panel.begin { [weak self] result in
            guard let self, result == .OK, let url = panel.url else { return }
            do {
                // Export the complete file, including lines outside the UI buffer.
                let data = try Data(contentsOf: URL(fileURLWithPath: self.logPath))
                try data.write(to: url, options: .atomic)
            } catch { self.operationError = "导出失败：\(error.localizedDescription)" }
        }
    }

    func shutdown() async {
        guard !shuttingDown else { return }
        shuttingDown = true
        crbot_shutdown(backend)
        while crbot_shutdown_complete(backend) == 0 {
            try? await Task.sleep(for: .milliseconds(100))
        }
        crbot_destroy(backend)
        backend = nil
    }
}
