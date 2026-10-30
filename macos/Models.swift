import Foundation

struct DeviceSnapshot: Decodable, Identifiable, Equatable {
    let serial: String
    let status: String
    let running: Bool
    let error: Bool
    let awaitingInput: Bool
    let pending: Bool
    var id: String { serial }
    var phase: String {
        if pending { return "正在重试" }
        if awaitingInput { return "等待确认" }
        if error { return "异常" }
        return running ? "运行中" : "已停止"
    }
}

struct LogLine: Decodable, Identifiable, Equatable {
    let id: UInt64
    let text: String
    var isError: Bool {
        let value = text.lowercased()
        return ["error", "failed", "timeout", "rejected", "unavailable", "unauthorized", "offline"]
            .contains { value.contains($0) }
    }
}

struct BackendSnapshot: Decodable {
    let discovering: Bool
    let logPath: String
    let message: String
    let error: String
    let devices: [DeviceSnapshot]
    let logs: [LogLine]
}

enum DeviceAction: Int32 {
    case retry = 1, stop, wait, disconnect
}
