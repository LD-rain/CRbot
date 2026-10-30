import Foundation

enum Failure: Error { case assertion(String) }

@main
struct BridgeSmoke {
    static func main() throws {
        let root = URL(fileURLWithPath: CommandLine.arguments[1])
        let result = root.appendingPathComponent("result.txt")
        do {
            try run(root)
            try "PASS: discovery, dedup, manual connection, error recovery, stop/retry, wait/disconnect, logs, cancellation, shutdown\n".write(to: result, atomically: true, encoding: .utf8)
        } catch {
            try "FAIL: \(error)\n".write(to: result, atomically: true, encoding: .utf8)
            exit(1)
        }
    }

    static func run(_ root: URL) throws {
        guard let backend = crbot_create(root.path) else { throw Failure.assertion("create backend") }
        defer { crbot_destroy(backend) }
        func mode(_ name: String) throws {
            try name.write(to: root.appendingPathComponent("mode"), atomically: true, encoding: .utf8)
        }
        func snapshot() throws -> BackendSnapshot {
            guard let ptr = crbot_snapshot(backend) else { throw Failure.assertion("snapshot allocation") }
            defer { crbot_free_string(ptr) }
            return try JSONDecoder().decode(BackendSnapshot.self, from: Data(String(cString: ptr).utf8))
        }
        func check(_ condition: Bool, _ detail: String) throws {
            if !condition { throw Failure.assertion(detail) }
        }
        func wait(_ detail: String, seconds: TimeInterval = 15, _ predicate: (BackendSnapshot) -> Bool) throws {
            let deadline = Date().addingTimeInterval(seconds)
            repeat {
                if predicate(try snapshot()) { return }
                Thread.sleep(forTimeInterval: 0.05)
            } while Date() < deadline
            throw Failure.assertion("timeout: \(detail), snapshot: \(try snapshot())")
        }
        func scan(_ endpoint: String = "") throws {
            crbot_discover(backend, endpoint)
            try wait("discovery") { !$0.discovering }
        }

        try mode("empty")
        try scan()
        try check(try snapshot().devices.isEmpty, "empty discovery must keep backend alive")
        try mode("server-error")
        try scan()
        try check(try snapshot().error.contains("fake server failure"), "server error surfaced")
        try mode("normal")
        try scan("127.0.0.1:5556")
        try check(try snapshot().error.contains("Connection refused"), "connection error surfaced")
        try scan("127.0.0.1:5555")
        try check(try snapshot().devices.count == 2, "identity dedup")
        try check(try snapshot().error.isEmpty, "retry clears discovery errors")
        try scan()
        try check(try snapshot().devices.count == 2, "rescan doesn't duplicate")
        try wait("UTF-8 and unauthorized logs") { $0.logs.contains { $0.text.contains("请检查设备授权") } }
        let displayedLogs = try snapshot().logs
        let timestampPattern = #"^\[\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}\] "#
        let persistedLogs = try String(contentsOf: root.appendingPathComponent("crbot.log"), encoding: .utf8)
        try check(!displayedLogs.isEmpty && displayedLogs.allSatisfy {
            $0.text.range(of: timestampPattern, options: .regularExpression) != nil
                && persistedLogs.contains($0.text + "\n")
        }, "GUI timestamps match the persisted log")

        try mode("bad-resolution")
        crbot_device_action(backend, "fake-1", 1)
        try wait("resolution rejected") { $0.devices.first?.error == true && $0.devices.first?.running == false }
        try check(try snapshot().devices[0].status.contains("800x1280"), "resolution details")
        try mode("slow-capture")
        crbot_device_action(backend, "fake-1", 1)
        let captureDeadline = Date().addingTimeInterval(5)
        while !FileManager.default.fileExists(atPath: root.appendingPathComponent("capture-started").path) {
            if Date() > captureDeadline { throw Failure.assertion("initial capture did not start") }
            Thread.sleep(forTimeInterval: 0.05)
        }
        crbot_device_action(backend, "fake-1", 2)
        try wait("cancel initial screenshot", seconds: 3) { !$0.devices[0].running && !$0.devices[0].error }

        try mode("normal")
        crbot_device_action(backend, "fake-1", 1)
        crbot_device_action(backend, "fake-2", 1)
        try wait("multi-device start") { $0.devices.allSatisfy(\.running) && $0.devices.allSatisfy { !$0.pending } }
        crbot_device_action(backend, "fake-1", 1)
        crbot_stop_all(backend)
        try wait("stop wins over retry", seconds: 5) { $0.devices.allSatisfy { !$0.running && !$0.pending } }
        Thread.sleep(forTimeInterval: 0.3)
        try check(try snapshot().devices.allSatisfy { !$0.running }, "no late restart")

        crbot_device_action(backend, "fake-1", 1)
        try wait("login prompt", seconds: 85) { $0.devices[0].awaitingInput }
        crbot_device_action(backend, "fake-1", 3)
        try wait("continue waiting") { !$0.devices[0].awaitingInput && $0.devices[0].running }
        crbot_device_action(backend, "fake-1", 4)
        try wait("disconnect") { !$0.devices[0].running }
        let diskLog = try String(contentsOf: root.appendingPathComponent("crbot.log"), encoding: .utf8)
        try check(diskLog.contains("测试 Phone"), "UTF-8 log persistence")
        try check(diskLog.contains("[UI] loaded"), "templates loaded")
        crbot_clear_logs(backend)
        try check(try snapshot().logs.isEmpty, "clear display")
        try check(try String(contentsOf: root.appendingPathComponent("crbot.log"), encoding: .utf8).contains("测试 Phone"), "clear preserves file")

        try mode("slow-server")
        crbot_discover(backend, "")
        Thread.sleep(forTimeInterval: 0.3)
        crbot_cancel_discovery(backend)
        try wait("cancel scan with partial pipe output", seconds: 3) { !$0.discovering }
        try mode("normal")
        try scan()
        try check(try snapshot().error.isEmpty, "cancelled scan can retry")
        crbot_device_action(backend, "fake-1", 1)
        crbot_shutdown(backend)
        let deadline = Date().addingTimeInterval(5)
        while crbot_shutdown_complete(backend) == 0 {
            if Date() > deadline { throw Failure.assertion("shutdown timeout") }
            Thread.sleep(forTimeInterval: 0.05)
        }
    }
}
