import SwiftUI

struct DashboardView: View {
    @Bindable var store: BotStore

    var body: some View {
        NavigationSplitView {
            VStack(alignment: .leading, spacing: 16) {
                HStack(spacing: 10) {
                    Image(systemName: "iphone.radiowaves.left.and.right")
                        .font(.title2).foregroundStyle(.tint)
                    VStack(alignment: .leading) {
                        Text("CRBot").font(.title2.bold())
                        Text("设备控制台").font(.caption).foregroundStyle(.secondary)
                    }
                }
                .padding(.horizontal, 16).padding(.top, 20)

                HStack {
                    Text("设备").font(.headline)
                    Spacer()
                    Text("\(store.devices.count)").foregroundStyle(.secondary)
                }.padding(.horizontal, 16)

                if store.devices.isEmpty {
                    VStack(alignment: .leading, spacing: 8) {
                        Image(systemName: "cable.connector").font(.largeTitle)
                        Text(store.discovering ? "正在寻找设备" : "尚未连接设备").font(.headline)
                        Text("开启 USB 调试并允许授权，或输入模拟器地址连接。")
                            .font(.callout).foregroundStyle(.secondary)
                    }.padding(16)
                    Spacer()
                } else {
                    List(selection: $store.selectedSerial) {
                        ForEach(store.devices) { device in
                            DeviceSidebarRow(device: device).tag(device.serial)
                        }
                    }.listStyle(.sidebar)
                }
                VStack(alignment: .leading, spacing: 7) {
                    Label("\(store.runningCount) 台运行中", systemImage: "play.circle.fill")
                    Label("\(store.waitingCount) 台等待确认", systemImage: "person.crop.circle.badge.questionmark")
                    Divider()
                    Text("要求 720 × 1280\nGPL-3.0-or-later")
                        .font(.caption2).foregroundStyle(.secondary)
                }.font(.caption).padding(16)
            }
            .navigationSplitViewColumnWidth(min: 210, ideal: 245, max: 320)
        } detail: {
            VStack(alignment: .leading, spacing: 18) {
                connectionBar
                if !store.error.isEmpty {
                    Label(store.error, systemImage: "exclamationmark.triangle.fill")
                        .font(.callout).foregroundStyle(.orange)
                        .textSelection(.enabled)
                        .padding(12).frame(maxWidth: .infinity, alignment: .leading)
                        .background(.orange.opacity(0.09), in: RoundedRectangle(cornerRadius: 10))
                }
                if let device = store.selectedDevice {
                    DeviceDetailView(device: device) { store.perform($0, on: device) }
                } else {
                    VStack(alignment: .leading, spacing: 10) {
                        Text("连接设备，开始自动对战").font(.title2.bold())
                        Text("扫描会发现 USB 真机与本地模拟器。选中设备后点击“启动”，运行已有的登录、选模式、对战与结算流程。")
                            .foregroundStyle(.secondary)
                        Text("没有找到设备时，请检查 ADB 是否安装、设备是否已授权，以及分辨率是否为 720 × 1280。")
                            .font(.callout).foregroundStyle(.secondary)
                    }.padding(20).frame(maxWidth: .infinity, alignment: .leading)
                        .background(.quaternary.opacity(0.5), in: RoundedRectangle(cornerRadius: 12))
                }
                LogConsoleView(store: store)
            }
            .padding(22)
            .background(Color(nsColor: .windowBackgroundColor))
            .navigationTitle("运行总览")
        }
        .disabled(store.shuttingDown)
        .overlay {
            if store.shuttingDown {
                ProgressView("正在停止任务并退出…").padding(28)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 14))
            }
        }
        .alert("操作失败", isPresented: Binding(
            get: { store.operationError != nil },
            set: { if !$0 { store.operationError = nil } }
        )) { Button("好", role: .cancel) { store.operationError = nil } }
        message: { Text(store.operationError ?? "") }
    }

    private var connectionBar: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack(spacing: 10) {
                TextField("设备地址，例如 127.0.0.1:5555", text: $store.endpoint)
                    .textFieldStyle(.roundedBorder)
                    .onSubmit { store.connect() }
                    .accessibilityIdentifier("connectionAddress")
                Button("连接") { store.connect() }
                    .disabled(store.discovering || store.endpoint.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty)
                Button { store.scan() } label: { Label("扫描设备", systemImage: "arrow.clockwise") }
                    .disabled(store.discovering)
                if store.discovering {
                    Button("取消") { store.cancelScan() }
                }
            }
            HStack(spacing: 8) {
                if store.discovering { ProgressView().controlSize(.small) }
                Text(store.message).font(.caption).foregroundStyle(.secondary)
                    .textSelection(.enabled)
            }
        }
    }
}

private struct DeviceSidebarRow: View {
    let device: DeviceSnapshot
    var body: some View {
        HStack(spacing: 10) {
            Image(systemName: "iphone").font(.title2)
            VStack(alignment: .leading, spacing: 5) {
                Text(device.serial).font(.system(.callout, design: .monospaced)).lineLimit(1)
                HStack(spacing: 5) {
                    Circle().fill(deviceColor(device)).frame(width: 6, height: 6)
                    Text(device.status == "Ready" ? "已连接 · 待启动" : device.phase)
                }.font(.caption).foregroundStyle(.secondary)
            }
        }.padding(.vertical, 6)
    }
}

func deviceColor(_ device: DeviceSnapshot) -> Color {
    if device.error { return .red }
    if device.awaitingInput { return .orange }
    return device.running ? .green : .secondary
}
