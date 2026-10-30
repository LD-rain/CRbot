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
  </a>

  <h3 align="center">CRBot</h3>

  <p align="center">
    《皇室战争》国服 1v1 自动对战机器人
    <br />
    Windows / macOS · 多开模拟器
    <br />
    <br />
    <a href="https://github.com/LD-rain/CRbot/releases"><strong>下载最新版本 »</strong></a>
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
    <li><a href="#关于项目">关于项目</a></li>
    <li>
      <a href="#快速开始">快速开始</a>
      <ul>
        <li><a href="#前置依赖">前置依赖</a></li>
        <li><a href="#安装">安装</a></li>
        <li><a href="#从源码构建">从源码构建</a></li>
      </ul>
    </li>
    <li><a href="#使用">使用</a></li>
    <li><a href="#版本与发布">版本与发布</a></li>
    <li><a href="#路线图">路线图</a></li>
    <li><a href="#参与贡献">参与贡献</a></li>
    <li><a href="#许可证">许可证</a></li>
    <li><a href="#联系方式">联系方式</a></li>
    <li><a href="#致谢">致谢</a></li>
  </ol>
</details>

## 关于项目

CRBot 通过 ADB 驱动 **720×1280** 的安卓模拟器（或真机），自动把《皇室战争》国服的对局跑起来：

* **全自动**：启动游戏 → 进入 1 对 1 → 出牌 → 结算 → 领奖 → 下一局，一直循环
* **多开**：一台机器同时跑多台设备，自动发现并按设备身份去重
* **不需要配置卡组**：换卡组、换账号都不用改设置
* **随时可停**：卡住了点 `Stop` 立即打断，点 `Retry` 整台重跑

出牌是节奏型：前 117 秒攒到 9 费出手，双倍圣水后降到 8 费并允许提前下灰卡，
落点固定在中场 `(360, 695)`，卡槽 1→4 轮转。**目前还不会防守**（不识别敌方部队与塔血）。

> 想了解判断依据和为什么不走 OCR，见 [设计文档](docs/圣水识别方案.md)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 快速开始

### 前置依赖

* **Windows 10/11 x64** 或 **macOS 11+（Apple Silicon）**
* 一台 720×1280 的安卓模拟器或真机（MuMu / 雷电 / Android Studio 模拟器 / USB 真机都行）
* 分辨率不对时先改：

  ```sh
  adb shell wm size 720x1280 && adb shell wm density 240
  ```

只有**从源码构建**才需要：Visual Studio 2022+（C++ 桌面开发）或 Xcode Command Line Tools、CMake ≥ 3.20。

### 安装

到 [Releases](https://github.com/LD-rain/CRbot/releases) 下载对应平台的包。

**Windows**

1. 解压 `CRBot-<版本>-win64.zip` 到任意目录
2. 双击 `CRbot.exe`
3. 别把 exe 单独拷出来——它要和旁边的 `assets/templates` 放在一起

**macOS**

1. 打开 `CRBot-<版本>-macos-arm64.dmg`，把 CRBot 拖进「应用程序」
2. 装 adb：

   ```sh
   brew install --cask android-platform-tools
   ```

3. 首次打开提示"无法验证开发者"时右键「打开」，或：

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

| 产物 | 用途 |
|---|---|
| `CRbot` | 主程序 |
| `elixir_probe` | 对一批截图跑圣水/手牌识别，用来验证改动 |
| `ui_probe` | 检查按钮模板在某张画面里的分数和位置 |

> 首次构建要几分钟编译内嵌的 adb；之后都是增量。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 使用

启动后自动扫描并连接模拟器，每台设备一行状态：

| 按钮 | 作用 |
|---|---|
| `Wait` | 登录超时等人工确认时出现，继续等 |
| `Disconnect` | 同上，断开这台设备 |
| `Stop` | 立即打断当前设备（连正在执行的 adb 命令一起结束） |
| `Retry` | 从"连设备"那一步整台重跑 |

日志写在 exe 同目录的 `crbot.log`；Release 版不弹控制台，Debug 版才弹。
卡住时会自动存一张现场截图（`crbot_debug.png` 等），排查问题时把它发出来最有用。

按钮模板都在 `assets/templates/`，外观变了往里加一张图即可，细节见 [assets/README.md](assets/README.md)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 版本与发布

* 版本号只写在 `VERSION`；发布包为 `CRBot-<版本>-win64.zip` 与 `CRBot-<版本>-macos-arm64.dmg`
* 功能改动走 PR 到 `dev`：只跑构建和测试，不发版
* PR 合并进 `dev`：发布**预发布** `X.Y.Z(n)`
* `dev` 合并进 `main`：发布**正式版** `X.Y.Z`（Latest）

打包脚本：`tools/package.ps1`（Windows）、`tools/package_macos.sh`（macOS）。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 路线图

- [x] 多开、设备自动发现与去重
- [x] 自动完成一局并循环（含结算与领奖）
- [x] 按钮外观变化可扩展（模板 + 掩膜）
- [x] Windows / macOS 双平台与自动发布
- [ ] **防守**：识别敌方部队与塔血，按威胁出牌
- [ ] 支持更多分辨率（现在固定 720×1280）
- [ ] 对局数据统计（胜率、圣水利用率）

详见 [open issues](https://github.com/LD-rain/CRbot/issues)。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 参与贡献

1. Fork 仓库
2. 建分支（`git checkout -b feature/AmazingFeature`）
3. 提交（`git commit -m 'Add some AmazingFeature'`）
4. 推送（`git push origin feature/AmazingFeature`）
5. 开 Pull Request 到 `dev`

改了识别相关的东西，请先用 `elixir_probe` / `ui_probe` 跑一遍离线验证再提交。

<p align="right">(<a href="#readme-top">回到顶部</a>)</p>

## 许可证

**GNU General Public License v3.0**（GPL-3.0），全文见 [LICENSE](LICENSE)。

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
