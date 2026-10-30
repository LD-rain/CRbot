<a id="readme-top"></a>

<!-- 项目徽章 -->
[![CI][ci-shield]][ci-url]
[![Version][version-shield]][releases-url]
[![License][license-shield]][license-url]
[![Platform][platform-shield]][releases-url]
[![Stars][stars-shield]][stars-url]
[![Issues][issues-shield]][issues-url]

<!-- 标题 -->
<br />
<div align="center">
  <a href="https://github.com/LD-rain/CRbot">
    <img src="assets/screens/1v1_icon0.png" alt="CRBot" width="72" height="84">
  </a>

  <h3 align="center">CRBot</h3>

  <p align="center">
    《皇室战争》国服 1v1 自动对战机器人
    <br />
    Windows / macOS · 多开模拟器 · 全像素识别，不用 OCR
    <br />
    <br />
    <a href="docs/圣水识别方案.md"><strong>阅读设计文档 »</strong></a>
    <br />
    <br />
    <a href="https://github.com/LD-rain/CRbot/releases">下载</a>
    &middot;
    <a href="https://github.com/LD-rain/CRbot/issues/new">报告问题</a>
    &middot;
    <a href="https://github.com/LD-rain/CRbot/issues/new">功能建议</a>
  </p>
</div>

<!-- 目录 -->
<details>
  <summary>目录</summary>
  <ol>
    <li>
      <a href="#关于项目">关于项目</a>
      <ul>
        <li><a href="#技术栈">技术栈</a></li>
        <li><a href="#它是怎么工作的">它是怎么工作的</a></li>
      </ul>
    </li>
    <li>
      <a href="#快速开始">快速开始</a>
      <ul>
        <li><a href="#前置依赖">前置依赖</a></li>
        <li><a href="#安装">安装</a></li>
        <li><a href="#从源码构建">从源码构建</a></li>
      </ul>
    </li>
    <li><a href="#使用">使用</a></li>
    <li><a href="#打包与发布">打包与发布</a></li>
    <li><a href="#路线图">路线图</a></li>
    <li><a href="#参与贡献">参与贡献</a></li>
    <li><a href="#许可证">许可证</a></li>
    <li><a href="#联系方式">联系方式</a></li>
    <li><a href="#致谢">致谢</a></li>
  </ol>
</details>

## 关于项目

CRBot 是一个通过 ADB 驱动 **720×1280** 安卓模拟器（或真机）的《皇室战争》国服机器人：
自动启动游戏、进入经典 1 对 1、按圣水节奏出牌、打完一整局、领完奖励再开下一局，一台机器可以同时跑多台设备。

界面判定**全部是像素级的**——圣水条、手牌、按钮都直接读画面，不碰游戏内存、不做 OCR。
所以换版本、换设备都不用重新适配，只要按钮外观变了补一张模板就行。

为什么这么做：

* **不依赖 OCR**：OCR 在游戏这种花哨背景上又慢又不稳，还受字体/分辨率影响；模板匹配单帧 2~5ms
* **不需要知道卡组**：读"卡牌被压暗"判断圣水够不够，换卡组、换账号都不用改配置
* **多开优先**：设备按 `android_id + serialno` 去重，常驻 adb shell、PNG 截图，5 开也不容易互相拖死
* **卡住能救**：GUI 上每台设备都有 `Stop`（立即打断，连 adb 命令一起杀）和 `Retry`（整台重跑）

当前出牌策略是节奏型：前 117 秒攒到 **9 费**出手，双倍圣水后降到 **8 费**并允许提前下灰卡，
落点固定在河边中央 `(360, 695)`，卡槽 1→4 轮转。**还不会防守**（不识别敌方部队和塔血）。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

### 技术栈

[![C++23][cpp-shield]][cpp-url]
[![CMake][cmake-shield]][cmake-url]
[![Win32][win32-shield]][win32-url]
[![SwiftUI][swiftui-shield]][swiftui-url]
[![ADB][adb-shield]][adb-url]
[![GitHub Actions][actions-shield]][actions-url]

核心逻辑是一套与平台无关的 C++（`src/`），平台相关的部分各写一份：

| 能力 | Windows | macOS |
|---|---|---|
| 界面 | Win32 窗口 | SwiftUI 应用（另有终端版） |
| 子进程 | `CreateProcess` | `posix_spawn` + `poll` |
| PNG 解码 | WIC | ImageIO |
| adb | 内嵌 `adb.exe`，运行时释放 | 系统 / Homebrew 的 `adb` |

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

### 它是怎么工作的

```
扫描模拟器端口 → 按设备身份去重 → 每台一个线程
   → 启动游戏 → 等主界面 → 选「经典 1 对 1」
   → 无限循环：开匹配 → [读圣水 + 读手牌 + 出牌] → 结算/领奖 → 回主界面
```

* **圣水**：底部圣水条固定在 `x 195..695, y 1238..1258`，10 格 × 50px。
  按颜色（填充 / 空槽 / 高光）逐列判断，取整到格，单帧 <1ms；
  滤波是"**下跌立即采信、上升受时间约束**"——扣费是瞬时的，上涨才不会误判。
* **手牌**：4 个卡位读"是否被压暗"：`Empty`（空格）/ `Unaffordable`（灰）/ `Ready`（能打）。
* **按钮**：归一化相关系数（NCC）模板匹配，粗扫 + 精修，**点击坐标一律取自匹配结果**。
  模板可以带 **`.mask`**（每行 `x y w h`），把"会变的像素"排除掉——
  比如对战按钮上的 `0/500` 计数、倒计时，模式图标右上角的红点角标（里面还有白色数字）。
* **两个循环**：对局内只认"圣水条出现/消失"，对局外只认按钮，两边互不依赖。

细节（5 种圣水方案的取舍、实测数据、时序约定）见 **[docs/圣水识别方案.md](docs/圣水识别方案.md)**。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 快速开始

### 前置依赖

* **Windows 10/11 x64** 或 **macOS 11+（Apple Silicon）**
* 一台 720×1280 的安卓模拟器或真机（MuMu / 雷电 / Android Studio 模拟器 / USB 真机都行）
* 真机或分辨率不对时先改：

  ```sh
  adb shell wm size 720x1280 && adb shell wm density 240
  ```

* 只有**从源码构建**才需要：Visual Studio 2022+（C++ 桌面开发）或 Xcode Command Line Tools、CMake ≥ 3.20

### 安装

到 [Releases](https://github.com/LD-rain/CRbot/releases) 下载对应平台的包：

**Windows**

1. 下载 `CRBot-<版本>-win64.zip`
2. 解压到任意目录，双击 `CRbot.exe`
3. **别把 exe 单独拷出来**——它按 `<exe>\assets\templates` 找按钮模板

**macOS**

1. 下载 `CRBot-<版本>-macos-arm64.dmg`，拖进「应用程序」
2. 装 adb：`brew install --cask android-platform-tools`
3. 只有 ad-hoc 签名，首次打开提示"无法验证开发者"时右键「打开」，或：

   ```sh
   xattr -dr com.apple.quarantine /Applications/CRBot.app
   ```

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

### 从源码构建

```sh
git clone https://github.com/LD-rain/CRbot.git
cd CRbot
```

**Windows**

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**macOS**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 8
```

产物：

| 目标 | 说明 |
|---|---|
| `CRbot` | 主程序 |
| `elixir_probe` | 离线回归：对一个目录的截图跑圣水/手牌识别 |
| `ui_probe` | 离线工具：验证按钮模板在一张画面里的分数和位置 |

> 首次构建会花几分钟编译 `src/adb_resource.cpp`（内嵌的 adb.exe），之后是增量秒级。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 使用

启动后会自动扫描并连接模拟器，每台设备一行状态：

| 按钮 | 作用 |
|---|---|
| `Wait` | 登录超时等人工确认时出现，继续等 |
| `Disconnect` | 同上，断开这台设备 |
| `Stop` | **立即打断**：停线程 + 杀掉正在执行的 adb 命令，不等 60 秒超时 |
| `Retry` | 从"连设备"那一步**整台重跑** |

日志写在 exe 同目录的 `crbot.log`；Release 版**不弹控制台**，Debug 版才会。
卡住时会自动存现场帧（`crbot_debug.png` / `crbot_battle_<serial>.png`），拿它就能量出按钮位置、补模板。

界面判定用的模板都在 `assets/templates/`，一个按钮可以挂多个外观：

```
btn_battle.png      主界面黄色「对战」        搜索范围 x210..520 y900..1120
btn_battle2.png     同上，带"每日额外奖励"小字
btn_battle3.png     同上，带皇冠计数 + 倒计时（掩膜掉动态数字）
btn_join.png        同上，免费模式的绿色「加入」+「免费」
btn_ok.png          结算页蓝色「确定」        搜索范围 x255..475 y1040..1180
btn_mode_1v1.png    模式菜单「经典1对1」      每次全屏重新找（列表会滚动）
btn_mode_icon.png   主界面"当前模式"图标      命中说明已经在 1v1，直接跳过菜单
```

离线验证模板：

```powershell
.\build\ui_probe.exe assets\screens\result2.png assets\templates\btn_ok.png
# found=1 score=1.0000 center=(87,36) rect=(0,0 175x73)
```

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 打包与发布

两个平台的打包脚本都只要对应平台的编译器：

```powershell
powershell -File tools\package.ps1                 # Windows → dist/CRBot-<版本>-win64.zip
powershell -File tools\package.ps1 -SkipBuild -BuildDir .\build-release
```

```sh
bash tools/package_macos.sh --skip-build --build-dir=build   # macOS → .dmg
```

版本号只写在 `VERSION` 里，CMake 和打包脚本都从它读。

**自动化**（`.github/workflows/build-release.yml`）：

| 操作 | 结果 |
|---|---|
| 功能分支 → `dev` 的 PR | CI 构建 + 测试 + 打包，只存 Actions artifact，**不发版** |
| PR 合并进 `dev` | CD 重新构建合并后的提交，发布**预发布** `X.Y.Z(n)` |
| `dev` → `main` 的 PR 合并 | CD 发布**正式版** `X.Y.Z`（Latest） |

* 预发布的 `n` 是**发布计数**（同版本已有预发布的最大 `n` + 1），所以是连续的 `(1)(2)(3)…`；
  同一次工作流重跑沿用原编号；
* 发布前会逐个校验包内容（9 个模板/掩膜文件、`VERSION`、`build-info.json`）和 SHA256；
* Release 附件只有包本体，`.sha256` 只在构建产物里用于核对，不上传。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 路线图

- [x] 圣水 / 手牌像素识别，不需要知道卡组
- [x] 多开 + 设备去重 + 常驻 adb shell
- [x] 全按钮模板化，点击坐标取自匹配结果
- [x] 模板掩膜（计数、倒计时、红点角标）
- [x] 跨平台（Windows / macOS）与自动发布流水线
- [ ] **防守**：识别敌方部队与塔血，按威胁出牌
- [ ] 支持更多分辨率（现在写死 720×1280）
- [ ] 卡组/费用表可选接入，做更聪明的圣水规划
- [ ] 对局数据统计（胜率、平均圣水利用率）

已知限制：只支持 720×1280；出牌是节奏型、不防守；赛季活动页是纯动画，只能靠点空白推进。

详见 [open issues](https://github.com/LD-rain/CRbot/issues)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 参与贡献

欢迎任何形式的贡献。想让这个项目更好：

1. Fork 仓库
2. 建分支（`git checkout -b feature/AmazingFeature`）
3. 提交（`git commit -m 'Add some AmazingFeature'`）
4. 推送（`git push origin feature/AmazingFeature`）
5. 开 Pull Request 到 `dev`

改代码前建议先看 [docs/圣水识别方案.md](docs/圣水识别方案.md)，里面写了为什么是现在这套方案。
改了识别相关的东西，请用 `elixir_probe` / `ui_probe` 跑一遍离线回归再提交。

如果只是"某个按钮点不到了"，最有用的反馈是卡住时自动存的 `crbot_debug.png`——一张真实 720×1280 帧，
能直接量出按钮位置、补一个模板。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 许可证

**GNU General Public License v3.0**（GPL-3.0），全文见 [LICENSE](LICENSE)。

强 copyleft：可以自由使用、修改、分发，但**分发（包括只发编译好的包）时必须同样以 GPL-3.0 开放完整源码**，
并保留版权与"无担保"声明。把本项目代码并入其它程序，那个程序也会被"传染"成 GPL-3.0。

内嵌的 `adb` 来自 Android SDK Platform-Tools，按 Apache-2.0 分发，见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 联系方式

项目地址：<https://github.com/LD-rain/CRbot>

问题与建议请开 [Issue](https://github.com/LD-rain/CRbot/issues)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 致谢

* **本项目的全部代码与文档，均由 DeepSeek 与 ChatGPT 编写。**
  人类提供需求、对局录像帧、按钮截图，以及在真机上跑日志、反馈问题。
* [Best-README-Template](https://github.com/othneildrew/Best-README-Template) —— 本 README 的结构参考
* [shields.io](https://shields.io) —— 徽章
* [Android SDK Platform-Tools](https://developer.android.com/tools/releases/platform-tools) —— adb
* [Clash Royale](https://supercell.com/en/games/clashroyale/) —— 被自动化的游戏（本项目与 Supercell 无关）

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

<!-- 链接与徽章 -->
[ci-shield]: https://img.shields.io/github/actions/workflow/status/LD-rain/CRbot/build-release.yml?branch=dev&style=for-the-badge&label=CI
[ci-url]: https://github.com/LD-rain/CRbot/actions/workflows/build-release.yml
[version-shield]: https://img.shields.io/github/v/release/LD-rain/CRbot?style=for-the-badge&label=version
[releases-url]: https://github.com/LD-rain/CRbot/releases
[license-shield]: https://img.shields.io/github/license/LD-rain/CRbot?style=for-the-badge
[license-url]: https://github.com/LD-rain/CRbot/blob/main/LICENSE
[platform-shield]: https://img.shields.io/badge/platform-Windows%20%7C%20macOS-blue?style=for-the-badge
[stars-shield]: https://img.shields.io/github/stars/LD-rain/CRbot?style=for-the-badge
[stars-url]: https://github.com/LD-rain/CRbot/stargazers
[issues-shield]: https://img.shields.io/github/issues/LD-rain/CRbot?style=for-the-badge
[issues-url]: https://github.com/LD-rain/CRbot/issues
[cpp-shield]: https://img.shields.io/badge/C%2B%2B-23-00599C?style=for-the-badge&logo=cplusplus&logoColor=white
[cpp-url]: https://en.cppreference.com/w/cpp/23
[cmake-shield]: https://img.shields.io/badge/CMake-3.20+-064F8C?style=for-the-badge&logo=cmake&logoColor=white
[cmake-url]: https://cmake.org
[win32-shield]: https://img.shields.io/badge/Win32-API-0078D6?style=for-the-badge&logo=windows&logoColor=white
[win32-url]: https://learn.microsoft.com/windows/win32/
[swiftui-shield]: https://img.shields.io/badge/SwiftUI-macOS-F05138?style=for-the-badge&logo=swift&logoColor=white
[swiftui-url]: https://developer.apple.com/xcode/swiftui/
[adb-shield]: https://img.shields.io/badge/ADB-platform--tools-3DDC84?style=for-the-badge&logo=android&logoColor=white
[adb-url]: https://developer.android.com/tools/adb
[actions-shield]: https://img.shields.io/badge/GitHub%20Actions-CI%2FCD-2088FF?style=for-the-badge&logo=githubactions&logoColor=white
[actions-url]: https://github.com/LD-rain/CRbot/actions
