import SwiftUI

struct LogConsoleView: View {
    @Bindable var store: BotStore

    var body: some View {
        VStack(alignment: .leading, spacing: 10) {
            HStack {
                Label("实时日志", systemImage: "terminal").font(.headline)
                Text("\(store.visibleLogs.count) 行").font(.caption).foregroundStyle(.secondary)
                Spacer()
                Toggle("自动滚动", isOn: $store.followLogs).toggleStyle(.checkbox)
                Menu {
                    Button("复制当前筛选日志") { store.copyLogs() }
                    Button("导出完整日志…") { store.exportLogs() }
                    Button("在 Finder 中显示") { store.revealLogs() }
                    Divider()
                    Button("清空显示") { store.clearLogs() }
                } label: { Image(systemName: "ellipsis.circle") }
                .menuStyle(.borderlessButton).fixedSize()
                .accessibilityLabel("日志操作")
            }
            HStack {
                TextField("搜索日志、设备序列号或关键字", text: $store.logQuery)
                    .textFieldStyle(.roundedBorder)
                Toggle("仅异常", isOn: $store.errorsOnly).toggleStyle(.checkbox)
            }
            GeometryReader { geometry in
                ScrollViewReader { proxy in
                    ScrollView([.vertical, .horizontal]) {
                        LazyVStack(alignment: .leading, spacing: 3) {
                            if store.visibleLogs.isEmpty {
                                Text(store.logs.isEmpty ? "等待日志…" : "没有匹配的日志")
                                    .foregroundStyle(.secondary).padding(.horizontal, 12)
                            }
                            ForEach(store.visibleLogs) { line in
                                Text(line.text.isEmpty ? " " : line.text)
                                    .fixedSize(horizontal: true, vertical: true)
                                    .foregroundStyle(line.isError ? Color.orange : Color.primary.opacity(0.85))
                                    .textSelection(.enabled)
                                    .padding(.horizontal, 12)
                                    .id(line.id)
                            }
                            // Anchor at x = 0 so following logs keeps the row inset.
                            Color.clear.frame(width: 1, height: 12).id("end")
                        }
                        .font(.system(size: 12, design: .monospaced))
                        // A horizontal scroll view proposes unbounded width. Keep
                        // short lines as wide as the viewport, anchored at its left.
                        .frame(minWidth: geometry.size.width, alignment: .leading)
                        .padding(.top, 12)
                    }
                    .defaultScrollAnchor(.topLeading)
                    .background(Color(nsColor: .textBackgroundColor), in: RoundedRectangle(cornerRadius: 10))
                    .overlay(RoundedRectangle(cornerRadius: 10).stroke(.separator.opacity(0.5)))
                    .onAppear {
                        if store.followLogs { proxy.scrollTo("end", anchor: .bottomLeading) }
                    }
                    .onChange(of: store.visibleLogs.last?.id) { _, _ in
                        if store.followLogs { proxy.scrollTo("end", anchor: .bottomLeading) }
                    }
                    .onChange(of: store.followLogs) { _, follow in
                        if follow { proxy.scrollTo("end", anchor: .bottomLeading) }
                    }
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }
}
