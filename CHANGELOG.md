# 更新日志

## 未发布 — macOS 界面精简

* macOS 日志统一添加本地时间毫秒级时间戳 `[YYYY-MM-DD HH:mm:ss.SSS]`，界面、复制及导出保持一致。
* 移除窗口右上角的全部停止按钮、日志底部说明及侧栏“不提供担保”文字。
* 修复日志在双向滚动区域中出现大块左侧留白的问题；自动跟随保持左对齐，保留长日志横向滚动。

## 未发布 — 原生安装包

* Windows 发布格式改为 Inno Setup `.exe` 安装程序，默认当前用户安装，提供快捷方式、原位更新和卸载，
  卸载保留运行时日志与截图；MSVC 静态链接运行库，无需用户单独安装 Visual C++ Redistributable。
* macOS 发布格式改为 `.dmg`，包含 `CRBot.app`、Applications 拖放入口及可选 `CLI` 文件夹。
* CI 增加 Windows 安装/重复安装/卸载验证；macOS 实际挂载 DMG 验证签名、架构、模板及构建元数据。
* CD 改为上传一个 Windows 安装程序和两个架构的 DMG，以及各自的 SHA256；不再发布 ZIP。
  `VERSION`、Build 递增和预发布/正式版本命名规则保持不变。

## 未发布 — Windows / macOS CI 与 CD

* 新增统一 GitHub Actions：功能分支 PR 到 `dev` 自动 CI，合并到 `dev` 自动预发布，
  本仓库 `dev` 合并到 `main` 自动正式发布；直接 push 不发布。
* 构建矩阵覆盖 Windows MSVC x64、macOS Apple Silicon 与 Intel，所有测试通过才发布。
* `VERSION` 管理 `X.X.X`，同一工作流的运行号作为跨平台共享 Build；本地应用构建自动递增本地 Build。
  预发布命名 `X.X.X(Y)`，正式版不显示括号及 Build。
* 打包读取实际构建的 `build-info.json`，添加 SHA256 和包结构验证；重新打包保持原 Build。
* 发布先准备草稿和全部附件，失败重跑可继续，禁止覆盖其它构建的既有版本。

## 未发布 — macOS SwiftUI GUI

* 新增 macOS 14+ 原生 `CRBot.app`：设备扫描/手动连接/去重、启动/停止/重试、
  登录超时继续等待/断开、多设备状态与全部停止，复用现有 C++ 机器人核心。
* 新增实时日志搜索、异常筛选、自动滚动、复制、清空显示、完整导出与 Finder 定位。
  日志和截图写入 `~/Library/Logs/CRBot/`。
* 无设备和 ADB 异常保留可恢复的窗口；后台扫描可取消，退出时先停止所有任务。
* CMake 同时生成 SwiftUI 应用和原终端版；打包脚本包含 `.app`、模板及许可证。
* 仅在 macOS 修复初始化截图阶段无法取消、POSIX 管道读取可能绕过超时、日志订阅析构竞争，
  将跨线程运行/错误标记改为原子变量；Windows 保持原有构建方式与运行行为。
* 新增假 ADB 集成测试，覆盖连接失败恢复、设备去重、分辨率拒绝、停止/重试竞争、
  登录等待/断开、日志和退出清理。

## 0.1.0 — 2026-10-25

macOS 原生支持，外加 Windows 侧的编译修复。

* **macOS 可原生编译运行**（Apple Silicon / Intel，clang + CMake），不再需要 MSVC。
* 新增平台层：`platform_*` 负责可执行文件目录与 adb 查找，`proc_*` 负责子进程与管道
  （Windows 用 `CreateProcess`，macOS 用 `posix_spawn` + `poll`）。两边都保留
  "不等管道 EOF、只等直接子进程退出"的逻辑，避免 adb 守护进程占着写端导致卡死。
* macOS 不内嵌 `adb.exe`，按 `CRBOT_ADB` > `PATH` > Android SDK / Homebrew 的顺序找系统 adb。
* PNG 解码在 macOS 上改用系统的 ImageIO（Windows 仍用 WIC）。
* 控制界面换成终端面板：设备状态 + 实时日志滚动区，按键 `s/r/w/d` 对应
  Stop / Retry / Wait / Disconnect，`q` 退出；没有终端时自动退化成行模式。
* 设备发现增加 `adb devices`，USB 真机和 Android Studio 的 `emulator-5554` 也能识别；
  端口扫描（5555~5599 等）保留给本地模拟器。
* 退出/Stop 现在会一起打断正在执行的 adb 命令，也会打断"等游戏启动"和"等主界面"的睡眠，
  不用再等满 60 秒。
* 修复 macOS 日志转发的两个问题：后台线程不能 detach 后随静态析构 terminate；
  控制台写入必须走 dup 出来的原始 stdout，否则日志会写回自己的管道形成死循环。
* 修复子进程管道 EOF 后 `poll` 一直返回 `POLLHUP` 导致的空转（29 个端口探测一度烧掉 7 秒 CPU）。
* 离线工具 `elixir_probe` / `ui_probe` 在两个平台都能编译；路径参数改用
  `std::filesystem::path`，以便同时支持 Windows 宽字符路径和 macOS 的 UTF-8 路径。
* Windows 侧补掉两处编译错误：`log_tee.cpp` 的 Windows 分支混入了 POSIX 的
  `shutdown_tee`，`probe_main.cpp` 缺少 `NOMINMAX` 导致 `std::max` 被宏顶掉。
  同时让 Windows 退出时也把管道里剩余的日志写完。
* 新增 `tools/package_macos.sh`：构建 Release、ad-hoc 签名并打包
  `dist/CRBot-<版本>-macos-<arch>.zip`。

## 0.0.1 (pre-release) — 2026-10-25

首个可运行的预发布版：多开模拟器、自动打完一整局并循环。

**许可证：[GPL-3.0](LICENSE)**（强 copyleft：分发二进制时须同样开放源码）。
内嵌的 adb 按 Apache-2.0 分发，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

### 对局内

* **圣水识别**：直接读底部圣水条（720x1280 实测 10 格 × 50px），
  单帧 <1ms；对比 2842 帧录像验证，读数与游戏大号数字逐帧一致。
* **手牌可打性**：读"卡牌被压暗"判断圣水够不够，**不需要知道卡组和费用**。
* **出牌策略**：前 117 秒 9 费出手；117 秒后（双倍圣水）降到 8 费，
  并允许"提前下"灰卡；卡槽 1→4 轮转，落点固定 (360,695)。
* 出牌后等「tap 命令返回 + 游戏处理完落点」才拍下一帧，避免连下两张。

### 对局外

* 与 OCR 完全解耦：主界面 / 结算页 / 模式菜单全部靠**按钮模板匹配**，
  点击坐标一律取自匹配结果。
* 一个按钮支持多个外观模板（例如「对战」按钮带不带"每日额外奖励"小字）。
* 结算后的每日奖励 / 开箱 / 赛季拼图按动画处理：只点空白处推进，绝不误触其它按钮。
* 多设备按 `android_id + serialno` 去重，避免多个线程抢同一块屏幕。

### 基础设施

* 常驻 `adb shell` 会话：`input tap` 从 ~150ms 降到 ~10ms。
* 截图走 PNG（~80KB）而非原始帧（3.7MB），多开时显著降低 adb 压力。
* 日志双写：控制台 + `crbot.log`（UTF-8，逐块刷新）。
* 卡住时自动存现场帧（`crbot_debug.png` / `crbot_battle_<serial>.png`）。
* GUI 每台设备一行：`Wait` / `Disconnect` / `Stop`（立即打断，连 adb 命令一起杀）/ `Retry`（整台重跑）。

### 已知限制

* 出牌没有防守逻辑（不识别敌方部队与塔血），只是节奏型压制。
* 分辨率固定 720x1280；其它分辨率需要改模板与几何。
* 模式菜单的开合按钮图标随模式变化，只能按固定位置点击。
