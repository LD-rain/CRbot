import SwiftUI

@MainActor
final class AppDelegate: NSObject, NSApplicationDelegate {
    let store = BotStore()

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
    }

    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        guard !store.shuttingDown else { return .terminateLater }
        Task {
            await store.shutdown()
            sender.reply(toApplicationShouldTerminate: true)
        }
        return .terminateLater
    }
}

@main
struct CRBotApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var delegate

    var body: some Scene {
        Window("CRBot", id: "dashboard") {
            DashboardView(store: delegate.store)
                .frame(minWidth: 920, minHeight: 640)
                .task { await delegate.store.observe() }
        }
        .defaultSize(width: 1180, height: 780)
        .commands {
            CommandGroup(after: .newItem) {
                Button("重新扫描设备") { delegate.store.scan() }
                    .keyboardShortcut("r", modifiers: [.command, .shift])
                    .disabled(delegate.store.discovering || delegate.store.shuttingDown)
                Button("停止全部任务") { delegate.store.stopAll() }
                    .keyboardShortcut(".", modifiers: .command)
                Divider()
                Button("显示日志文件") { delegate.store.revealLogs() }
                Button("导出完整日志…") { delegate.store.exportLogs() }
            }
        }
    }
}
