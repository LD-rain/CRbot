# CRBot

**版本 0.1.0** · [更新日志](CHANGELOG.md) · [圣水识别方案](docs/圣水识别方案.md) · **[GPL-3.0](LICENSE)**

> 🤖 **本项目的全部代码与文档完全由 DeepSeek 制作。**

《皇室战争》国服（`com.tencent.tmgp.supercell.clashroyale`）自动化机器人。
通过 ADB 驱动 **720×1280** 的模拟器 / 真机，自动循环打 1v1 对战。
Windows 上是 Win32 窗口，macOS 上提供原生 SwiftUI 窗口和终端面板，
都显示每台设备的状态，需要时可人工介入。

## 现在能做什么

```
扫描模拟器端口 → 按设备身份去重 → 每台一个线程
   → 启动游戏 → 等主界面（"对战"按钮出现）
   → 选「经典 1 对 1」
   → 无限循环：开匹配 → [读圣水 + 读手牌 + 自动出牌] → 结算/领奖 → 回主界面
```

* ✅ **多开**：扫描 5555~5599（步长 2）+ 16384/16416/7555/21503/62001/62025，
  按 `android_id + serialno` 去重（同一个模拟器会同时监听好几个端口）
* ✅ **圣水识别**：像素级读底部圣水条，0~10 整数格，单帧 <1ms
* ✅ **手牌可打性**：读"卡牌被压暗"判断圣水够不够，**不需要知道卡组和费用**
* ✅ **对局循环**：读圣水 → 选牌 → 点卡 → 落点 → 判结束 → 结算 → 下一局
* ✅ **界面判定全像素化**：主界面 / 结算页 / 模式菜单靠按钮模板匹配，**不用 OCR**
* ✅ **多开稳定**：常驻 adb shell、PNG 截图、日志落盘、卡住可 Stop/Retry

出牌策略（`BattleConfig`，见 `include/battler.h`）：

| 阶段 | 圣水阈值 | 选牌 |
|---|---|---|
| 前 117 秒 | **9 费** | 只下亮着的（`Ready`） |
| 117 秒后（双倍圣水） | **8 费** | 优先亮着的；没有就"提前下"灰卡，避免圣水溢出 |

落点固定 **(360, 695)**（河边中央），卡槽 **1 → 2 → 3 → 4** 轮转。
没有敌方部队/塔血识别，所以还谈不上"会防守"。

## 编译（Windows）

需要 Visual Studio（MSVC，C++23）和 CMake ≥ 3.20。
`adb.exe` 已经以二进制数组形式编进程序（`src/adb_resource.cpp`，所以**首次全量编译会慢几分钟**）。

```powershell
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug
```

| 目标 | 说明 |
|---|---|
| `CRbot.exe` | 主程序（Win32 GUI） |
| `elixir_probe.exe` | 离线回归：对一个目录的截图跑圣水/手牌识别 |
| `ui_probe.exe` | 离线工具：验证按钮模板在一个画面里的匹配分和位置 |

> Debug 版会弹控制台；Release 版不弹，日志只写 `crbot.log`（见下文）。

## 编译与运行（macOS）

GUI 需要 **macOS 14 或更高版本**、Xcode / Command Line Tools（Swift 6、clang、macOS SDK）、
CMake ≥ 3.20，以及一份 `adb`。支持 Apple Silicon 和 Intel，默认构建当前机器架构：

```bash
xcode-select --install                          # 没装过才需要
brew install --cask android-platform-tools      # 只装 adb，不需要整个 Android Studio
```

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
open build/CRBot.app               # 原生 SwiftUI 窗口，也可以在 Finder 双击
./build/CRbot                      # 可选：原终端面板

# 手边没有设备时，用假 adb 验证，不会操作真机
tools/smoke_macos.sh build
bash tools/smoke_macos_gui.sh build # GUI 桥接集成测试，包含真实的 60 秒登录超时
```

程序按顺序找 adb：`CRBOT_ADB` 环境变量 > `PATH` > `~/Library/Android/sdk/platform-tools/adb`
> Homebrew / MacPorts 的常见位置。GUI 找不到 ADB 时会显示安装提示，窗口保持打开，
安装后点击「扫描设备」即可重试。Finder 启动也会搜索 SDK 与 Homebrew 的常见目录。

### SwiftUI 设备控制台

* 启动后自动扫描 USB / 已连接设备与常见模拟器端口，按设备身份去重；
  **发现设备后由用户点击「启动」开始机器人任务**。
* 输入 `host:port` 手动连接；可以重新扫描或取消正在进行的扫描。
* 左侧选择设备，右侧查看实时状态：登录、选模式、匹配、圣水/手牌/出牌和结算。
* 「启动 / 重试」从连接及分辨率校验重新执行；「停止」取消当前任务；
  工具栏「全部停止」或 `⌘.` 停止所有设备。
* 登录等待超时时显示「继续等待 / 断开」。「断开」停止该设备的机器人，
  保留设备行供后续重试，不执行全局 `adb kill-server`。
* 实时日志支持搜索、异常筛选、自动滚动、复制筛选内容、清空显示、导出完整日志和 Finder 定位。
  界面保留最近 2,000 行，文件保留本次会话完整日志；每次启动会重建日志文件。
* 关闭窗口或 `⌘Q` 会取消扫描、停止机器人和正在执行的截图命令，再退出。

GUI 日志与调试截图位于 `~/Library/Logs/CRBot/`，无需在应用包里写文件。
终端版仍写当前工作目录。自动化测试可通过 `CRBOT_DATA_DIR` 改变 GUI 输出目录。
扫描和机器人运行在后台线程，SwiftUI 只读取快照与提交操作请求。

只需要终端版时，可配置 `-DCRBOT_BUILD_MACOS_GUI=OFF`，无需 Swift 编译器。
GUI 每次构建一个架构，可用 `-DCMAKE_OSX_ARCHITECTURES=arm64` 或 `x86_64` 指定。
SwiftUI、桥接库及相关核心调整仅在 Apple 平台启用；Windows 仍按原有方式构建
Win32 GUI、内嵌 ADB 和 WIC 图像解码，不需要 Swift、Xcode 或 Apple 框架。

和 Windows 版的差别：

| | Windows | macOS |
|---|---|---|
| 控制界面 | Win32 窗口（按钮） | SwiftUI 原生窗口 + 可选终端面板 |
| adb | 内嵌 `adb.exe`，解压后使用 | 用系统 / SDK 里的 `adb` |
| PNG 解码 | WIC | ImageIO |
| 设备发现 | 扫 5555~5599 等模拟器端口 | 同上 + `adb devices`（USB 真机、Android Studio 模拟器） |

终端面板按键：

| 按键 | 作用 |
|---|---|
| `↑` / `↓` 或 `j` / `k` | 选择设备 |
| `1`~`9` | 直接选第 N 台 |
| `s` | **Stop**：立即打断（连正在执行的 adb 命令一起杀掉，不等 60 秒超时） |
| `r` | **Retry**：从"连设备"那一步整台重跑 |
| `w` / `d` | 登录超时等人工确认时：继续等 / 断开这台 |
| `q` | 退出（会先停掉所有 adb 命令再退） |

日志照旧双写：终端面板里看到的是 `crbot.log` 的实时镜像。
stdin / stdout 不是终端时（CI、`nohup`）自动退化成行模式：状态变化按行打印，
命令用 `stop N` / `retry N` / `wait N` / `disconnect N` / `quit`。

### macOS 上没有 MuMu/雷电，怎么跑起来

MuMu、雷电都是 Windows 程序。macOS 上可以用 Android Studio 的模拟器或一台真机，
再把分辨率改成项目要求的 720×1280：

```bash
adb devices                       # 先确认设备连上了
adb shell wm size 720x1280        # 临时覆盖分辨率
adb shell wm density 240
```

`wm size` 是临时的，模拟器重启会恢复默认；想固定下来就在 AVD 里建一个 720×1280 的自定义设备。
改完用 `adb shell wm size` 确认输出 `Physical size: 720x1280`，
否则程序会在启动时报 `resolution ... != 720x1280` 并拒绝这台设备。

> Windows 的 `tools/package.ps1` 只负责打包 Windows 版；macOS 使用 `tools/package_macos.sh`。

## Windows 安装程序

从 Releases 下载 `CRBot-<版本>-win64-setup.exe` 并运行。默认安装到
`%LOCALAPPDATA%\Programs\CRBot`，无需管理员权限；提供开始菜单快捷方式、可选桌面快捷方式，
可在 Windows「已安装的应用」中卸载。同一安装位置再次运行安装程序即可更新，卸载保留运行时日志和截图。

本地打包需要 [Inno Setup 6.3+](https://jrsoftware.org/isinfo.php)；脚本会查找 `ISCC.exe`，
也可用 `-IsccPath 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'` 指定。

```powershell
# Windows PowerShell 或 pwsh 都行
powershell -File tools\package.ps1

# 已经用 CLion 构建过，只打包
powershell -File tools\package.ps1 -SkipBuild -BuildDir .\build-release
```

脚本会自己找 `vcvars64.bat` 和 CLion 自带的 cmake+ninja，构建 `RelWithDebInfo`，输出：

```
dist\CRBot-0.1.0(1)-win64-setup.exe
dist\CRBot-0.1.0(1)-win64-setup.exe.sha256
```

正式版本号只写在 `VERSION` 文件里；Build 号由构建过程自动产生。
包名、程序显示版本与 `build-info.json` 使用同一份构建元数据。
安装程序包含 `CRbot.exe`、模板、文档、许可证和构建信息；MSVC 使用静态 C/C++ 运行库，
运行 CRBot 无需另装 Visual C++ Redistributable。安装程序目前未配置 Authenticode 签名。

## macOS DMG 安装包

从 Releases 下载适合机器架构的 `.dmg`，打开后将 `CRBot.app` 拖入 `Applications`，
再从「应用程序」启动。更新前先退出 CRBot，替换旧应用后推出磁盘映像。
DMG 内的 `CLI` 是可选终端版；使用时将整个 `CLI` 文件夹复制到可写目录，再运行 `./CRbot`。

```bash
tools/package_macos.sh               # 构建 Release（build-release）并打包
tools/package_macos.sh --skip-build  # 只打包已有产物
```

输出：

```
dist/CRBot-0.1.0(1)-macos-arm64.dmg
dist/CRBot-0.1.0(1)-macos-arm64.dmg.sha256
```

磁盘映像根目录包含 `CRBot.app`、指向 `/Applications` 的快捷方式、`CLI`、安装说明、
文档、许可证和 `build-info.json`。Intel 构建对应 `macos-x86_64.dmg`。
打包过程使用系统 `hdiutil` 创建压缩只读映像，临时安装文件位于构建目录的 `installer-stage`。

脚本只做 **ad-hoc 签名**（`codesign --sign -`），没有 Apple 开发者签名和公证：
拷到别的 Mac 上、或者从浏览器下载后，Gatekeeper 仍可能拦一次，
需要右键「打开」，或者 `xattr -dr com.apple.quarantine <目录>`。

## CI / CD 与 Build 号

工作流：`.github/workflows/build-release.yml`。Windows 与 macOS 共用一条流水线及一个 Build 计数器。

| 操作 | 自动执行 | 版本显示 / Release |
|---|---|---|
| 任意功能分支 → `dev` 创建或更新 PR | CI：三个平台构建、测试、打包，保存 Actions artifacts | `1.1.0(3)`，不创建 Release |
| PR 合并到 `dev` | CD：重新构建合并后的提交，全部通过后发布 | `1.1.0(4)`，GitHub **Pre-release** |
| 同仓库 `dev` → `main` 创建或更新 PR | 正式发布前 CI | 带 Build 号的测试包，不创建 Release |
| 同仓库 `dev` → `main` 合并 | CD：重新构建合并后的提交，全部通过后发布 | 如 `VERSION` 为 `1.1.1`，发布 **1.1.1**，不显示 Build 号 |
| 直接 push 到 `dev` / `main`，没有匹配的已合并 PR | 跳过构建及发布 | 不创建 Release |

发布作业会查询推送提交对应的 PR，同时核对目标分支、合并 SHA；正式发布还核对来源是本仓库 `dev`。
三个平台是 `windows-2022`（MSVC / x64）、`macos-15`（Apple Silicon）、`macos-15-intel`（Intel）。
Windows 运行离线 PNG 解码、模板匹配及识别测试，并使用 Inno Setup 构建真实安装程序。
CI 在临时目录静默安装、重复安装，验证已安装文件、版本、快捷方式及卸载注册，再卸载并确认保留日志。
本地需要在没有安装 CRBot 的临时 Windows 环境执行 `tools/smoke_windows_installer.ps1`，避免覆盖日常安装。
macOS 还运行假 ADB 的完整桥接集成测试，并只读挂载 DMG 检查 Applications 快捷方式、
应用签名、架构、资源与版本元数据，检查结束后自动卸载映像。
全部平台测试及 SHA256 校验通过后，才把收齐安装包附件的 Release 草稿公开。

**版本规则：**

* `X.X.X` 从 `VERSION` 读取，发布前在 PR 内修改；不会自动增加补丁版本或向源代码分支回写提交。
* CI / CD 的 `Y` 使用同一个工作流的 `github.run_number`，从 1 开始，每次新运行加 1。
  一次矩阵构建的三个平台共享 `Y`；失败或取消的运行也占号，因此已发布版本的 Build 号可能不连续。
* 重新运行同一次工作流视为重试，沿用原 Build 号。重试可补全本次发布的草稿；已发布成功则不重复发布。
  不要将 CI/CD 拆成独立计数的工作流或更换此工作流文件来重置计数。
  此行为遵循 [GitHub 的 run_number / run_attempt 定义](https://docs.github.com/en/actions/reference/workflows-and-actions/contexts#github-context)。
* 预发布标题为 `1.1.0(3)`，tag 为 `v1.1.0(3)`；正式版标题为 `1.1.1`，tag 为 `v1.1.1`。
  已存在的正式版本不会被覆盖，需要先修改 `VERSION` 再发布。
* 预发布包名示例：`CRBot-1.1.0(3)-win64-setup.exe`、`CRBot-1.1.0(3)-macos-arm64.dmg`、
  `CRBot-1.1.0(3)-macos-x86_64.dmg`。正式包为 `CRBot-1.1.1-win64-setup.exe` 与
  `CRBot-1.1.1-macos-<架构>.dmg`。
* 每个安装包附带 `.sha256`；安装后的目录 / DMG 内的 `build-info.json` 记录版本、Build、发布通道、配置和提交 SHA。
  正式包内部仍保留 Build 用于追踪，但名称和程序显示版本不带括号。
  Release 附件显示名称保持上述格式；如果 GitHub 规范化了实际下载文件名，校验文件会同步采用返回的名称
  （[GitHub 附件命名规则](https://docs.github.com/en/rest/releases/assets#upload-a-release-asset)）。
* 本地默认 `CRBOT_BUILD_NUMBER=AUTO`：每次构建应用时增加该构建目录的计数，保存在 `build-number.txt`；
  删除构建目录会重置本地计数。本地计数与 Actions 计数相互独立。
  `--skip-build` / `-SkipBuild` 仅按已有元数据重新打包，不增加 Build。
* GitHub Release 附件直接提供 `.exe` / `.dmg`，不再发布 ZIP。Actions 的 artifact 下载外层由 GitHub
  自动封装，解开后得到上述安装包及校验文件；这不改变 Release 的安装包格式。

本地构建正式包：

```bash
tools/package_macos.sh --channel=stable
```

```powershell
pwsh -File tools\package.ps1 -Config Release -Channel stable
```

CI 使用 `-DCRBOT_BUILD_NUMBER=<Y> -DCRBOT_RELEASE_CHANNEL=prerelease|stable` 固定同一次构建的元数据。
macOS 的 `CFBundleShortVersionString` 保留 `X.X.X`，`CFBundleVersion` 使用整数 Build。
Windows 使用 MSVC 静态运行库以支持独立安装；SwiftUI / Apple 框架依然不会进入 Windows 目标。

工作流提交到仓库后即可自动触发。发布作业只使用 `GITHUB_TOKEN` 的 `contents: write` 权限，无需额外 PAT；
PR 构建作业保持只读权限。可以在 `dev` / `main` 的分支规则中把 **CI checks** 设为合并必需检查。
这里提供脚本，不会自动更改仓库分支保护规则。

本地验证流水线脚本：

```bash
node --test tests/ci/*.test.cjs
python3 -m unittest discover -s tests/ci -p 'test_*.py' -v
```

## 离线回归

```powershell
# 对录像拆出来的帧逐帧识别，输出 CSV：文件名,条可见,圣水格数,填充比例,手牌
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" > out.csv

# 只看每 10 帧
.\cmake-build-debug\elixir_probe.exe "D:\...\VideoRecords\frames" 10
```

不带参数也不会报错退出：依次找 `%CRBOT_FRAMES%`、exe 往上各层的 `frames\`、
`%USERPROFILE%\Documents\MuMu共享文件夹\VideoRecords\frames`；都找不到才打印用法（返回 0）。
在 CLion 里想直接跑，把帧目录填进 **Run → Edit Configurations → Program arguments**。

按钮模板也可以用 `ui_probe` 单独验证：

```powershell
.\cmake-build-debug\ui_probe.exe assets\screens\result2.png assets\templates\btn_ok.png
# → found=1 score=1.0000 center=(361,1120) rect=(274,1084 175x73)
```

## 目录

```
include/ src/
  adb_client.*     adb 调用（connect / screencap / tap / swipe / shell）
                   常驻 shell 会话、可被打断的子进程
  proc_win.cpp     子进程 + 管道（CreateProcess / HANDLE）
  proc_posix.cpp   子进程 + 管道（posix_spawn / poll，可被 cancel 打断）
  platform_win.cpp     可执行文件目录、路径拆分等小工具（Windows）
  platform_posix.cpp   同上 + adb 查找（macOS：CRBOT_ADB > PATH > SDK / Homebrew）
  adb_resolve_win.cpp  内嵌 adb.exe 的提取（仅 Windows）
  adb_resource.*   内嵌的 adb.exe（仅 Windows，macOS 不编译）
  png_io_wic.cpp   PNG 解码：WIC（Windows）
  png_io_imageio.cpp PNG 解码：ImageIO（macOS）
  device.*         设备连接 + 分辨率校验（必须 720x1280）+ 截图（PNG 优先）
  image.*          RGBA 帧、screencap 解析、PNG 落盘（调试用）
  template_match.* ★ 归一化相关（NCC）模板匹配，粗扫 + 精修
  ui_buttons.*     ★ 按钮模板组、搜索范围、匹配
  elixir.*         ★ 圣水条几何 + 读数（纯函数）
  hand.*           ★ 4 个卡位的可打性（纯函数）
  battler.*        ★ 对局中循环：读圣水、读手牌、出牌（不认识 GUI）
  navigator.*      对局外：主界面导航、start_match、finish_match
  cr_game.*        进程/包名、启动游戏、等主界面
  bot_runner.*     单台设备的线程主流程 + Stop/Retry
  gui_dashboard.*  Win32 状态面板（仅 Windows）
  tui_dashboard.*  macOS 终端面板 + 日志滚动区
  log_tee.*        控制台 + crbot.log 双写（macOS 可挂日志订阅）
  probe_main.cpp   离线回归工具入口（圣水/手牌）
  ui_probe.cpp     离线工具：验证按钮模板
macos/
  CRBotApp.swift       SwiftUI 入口、菜单与退出生命周期
  BotStore.swift       主线程可观察状态、日志文件操作
  *View.swift          设备列表、控制面板、实时日志
  CRBotBridge.*        C ABI 桥接、后台扫描、机器人调度、日志快照
  Info.plist.in        macOS 应用包元数据
tests/macos/
  fake_adb.py          不接触真实设备的 ADB 替身
  BridgeSmoke.swift    桥接层与核心流程集成测试
assets/
  templates/       按钮模板（构建时复制到 exe 旁边）
  screens/         原始按钮截图（留档，补模板时用）
docs/
  圣水识别方案.md   设计说明：5 种圣水方案取舍 + 实测数据
tools/
  package.ps1      构建 + 打包预发布版
  *.py             量取几何、验证读数用的离线脚本
```

## 界面判定：像素模板，不用 OCR

主界面 / 结算页 / 菜单项全部靠模板匹配，**点击坐标一律取自匹配结果**：

```
btn_battle.png      主界面黄色「对战」   搜索范围 x210..520 y900..1120
btn_battle2.png     同上，带"每日额外奖励"小字的外观
btn_ok.png          结算页蓝色「确定」   搜索范围 x255..475 y1040..1180
btn_mode_1v1.png    模式菜单「经典1对1」 每次全屏重新找（列表会滚动）
```

* 一个按钮可以挂**多个外观模板**，取最高分——按钮外观会随账号状态/活动/版本变化；
* 「经典1对1」**不缓存**：模式列表会滚动，位置每次都变，所以每次全屏搜索
  （粗扫步长 8，约 0.2~0.5s），找不到就滑动列表再找；
* 模式菜单的开合按钮图标随当前模式变、还会随版本改，没法做模板，只能按固定位置 (570,1000) 点；
* 阈值 `0.72 / 0.70 / 0.80`，见 `src/navigator.cpp` 顶部常量；
* 模板缺失时所有判定都会失败，日志会打 `[UI] button templates unavailable`——
  这是唯一需要留意的前置条件。

## 日志与调试产物

| 文件 | 内容 |
|---|---|
| `crbot.log` | 完整日志（UTF-8 BOM，逐块刷新，崩了也不丢） |
| `crbot_debug.png` | 结算流程卡住时存的现场帧 |
| `crbot_battle_<serial>.png` | 等对局开始超时时存的现场帧 |

* **Windows Debug**：弹控制台 + 写 `crbot.log`；
* **Windows Release**：**不弹控制台**（`NDEBUG` 下跳过 `AllocConsole`），日志只落 `crbot.log`。
  不想要文件日志就删掉 `main.cpp` 里 `start_log_tee("crbot.log")` 那一行。
* **macOS GUI**：窗口实时显示，同时写 `~/Library/Logs/CRBot/crbot.log`。
  macOS GUI / 终端版的日志均带本地时间毫秒级前缀，例如
  `[2026-10-27 14:23:45.123] [DEVICE] emulator-5556 ...`。
  时间取自日志管道收到该行首段内容的时刻，界面、复制内容与导出文件使用同一时间戳。

截图走 **PNG**（约 80KB）而不是原始帧（3.7MB）：多开时瓶颈是 adb 通道，PNG 快好几倍；
解码失败会自动退回原始帧。单帧超过 2 秒会打 `slow screenshot` 警告。

## 卡住时怎么办

GUI 每台设备一行，右侧按钮：

| 按钮 | 作用 |
|---|---|
| `Wait` | 登录超时等人工确认时出现，继续等 |
| `Disconnect` | 同上，断开这台设备 |
| `Stop` | **立即打断**：停线程 + 杀掉正在执行的 adb 命令，不等 60 秒超时 |
| `Retry` | 从"连设备"那一步**整台重跑**（复用启动流程） |

按钮只登记请求，真正的 stop/join/start 由主进程里的监视线程执行——
界面线程从不阻塞，所以机器人卡在 adb 调用里时窗口也不会假死。

macOS 的终端面板按键和上面一一对应：`s` = Stop，`r` = Retry，`w` = Wait，
`d` = Disconnect，`q` = 退出。退出时也会先把正在跑的 adb 命令杀掉，
不会卡在 60 秒超时上。

## 真机上第一次跑，重点看这几行日志

1. `[CRBOT] version 0.1.0` + 4 行 `[UI] loaded btn_*.png` —— 素材就位；
2. `[NAV] battle button at (365,1000) score 0.9x, tapping` —— 主界面识别到了；
   没生效会自动 `tap did not take effect, retry 1/3`；
3. `[NAV] 经典1对1 at (358,973) score 0.9x` —— 模式菜单选对了；
4. `[BATTLE] play slot N at (360,695)` + `[BATTLE] frame avg ...ms` —— 出牌与帧耗时。

## 已知限制

* 只支持 720×1280；换分辨率要改模板和 `elixir.cpp` / `hand.cpp` 里的几何；
* 出牌是节奏型，不防守、不针对卡组；
* 赛季专属活动页是纯动画，只能靠点空白处推进；
* 详见 [CHANGELOG.md](CHANGELOG.md) 的"已知限制"。

## 许可证

**GNU General Public License v3.0**（GPL-3.0），全文见 [LICENSE](LICENSE)。

GPL-3.0 是**强 copyleft（传染性）**许可证：

* 你可以自由使用、修改、分发；
* 但**分发（包括只发编译好的 exe）时必须同样以 GPL-3.0 开放完整源码**，
  并保留版权声明与"无担保"声明；
* 把本项目代码并入其它程序，那个程序也会被"传染"成 GPL-3.0。

> 也就是说：**发二进制就要给源码**。本仓库就是源码，分发时把仓库地址一并给出即可。

内嵌的 `adb` 来自 Android SDK Platform-Tools，按 Apache-2.0 分发，
其版权与许可声明见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 关于

本项目的全部代码与文档**完全由 DeepSeek 制作**——从圣水条的像素几何标定、模板匹配、对局/对局外
两个循环的设计，到 README 与识别方案文档，均由 DeepSeek 生成并迭代完成。

人类提供的是需求、对局录像帧、按钮截图，以及在真机上跑日志、反馈问题。
包括"卡牌压暗即不可打""双倍圣水该降到 8 费""奖励页是动画只能点空白处"这些关键结论，
也是在一轮轮真实日志反馈中被纠正和沉淀下来的。

