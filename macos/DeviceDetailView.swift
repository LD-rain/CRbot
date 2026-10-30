import SwiftUI

struct DeviceDetailView: View {
    let device: DeviceSnapshot
    let action: (DeviceAction) -> Void

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                VStack(alignment: .leading, spacing: 5) {
                    Text(device.serial).font(.title2.bold()).textSelection(.enabled)
                    Text("自动登录 · 经典 1 对 1 · 循环对战").font(.caption).foregroundStyle(.secondary)
                }
                Spacer()
                Label(device.status == "Ready" ? "待启动" : device.phase, systemImage: "circle.fill")
                    .font(.caption.bold()).foregroundStyle(deviceColor(device))
                    .padding(.horizontal, 10).padding(.vertical, 6)
                    .background(deviceColor(device).opacity(0.1), in: Capsule())
            }
            Text(device.status).font(.system(.body, design: .monospaced))
                .textSelection(.enabled).frame(maxWidth: .infinity, alignment: .leading)
            HStack(spacing: 10) {
                Button { action(.retry) } label: {
                    Label(device.running || device.error ? "重试" : "启动", systemImage: "play.fill")
                }
                .buttonStyle(.borderedProminent).disabled(device.pending)
                .help("从连接、分辨率校验、启动游戏重新运行")
                Button { action(.stop) } label: { Label("停止", systemImage: "stop.fill") }
                    .disabled(!device.running && !device.pending)
                Spacer()
                if device.pending { ProgressView().controlSize(.small) }
            }
            if device.awaitingInput {
                Divider()
                HStack {
                    Label("登录等待超时，请在设备上完成登录。", systemImage: "person.crop.circle.badge.questionmark")
                        .font(.callout).foregroundStyle(.orange)
                    Spacer()
                    Button("继续等待") { action(.wait) }
                    Button("断开") { action(.disconnect) }
                        .help("停止此设备的机器人任务，保留设备以便重新启动")
                }
            }
        }
        .padding(20)
        .background(.background, in: RoundedRectangle(cornerRadius: 12))
        .overlay(RoundedRectangle(cornerRadius: 12).stroke(.separator.opacity(0.5)))
    }
}
