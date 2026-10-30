#include "adb_client.h"
#include "bot_runner.h"
#include "device_state.h"
#include "log_tee.h"
#include "platform.h"
#include "crbot_version.h"

#if defined(_WIN32)
#include <windows.h>

#include "gui_dashboard.h"
#else
#include "tui_dashboard.h"
#endif

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <list>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef CRBOT_VERSION
#define CRBOT_VERSION "dev"
#endif

namespace {

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == ' ') ++i;
    return s.substr(i);
}

// 同一个模拟器可能被好几个端口（5557 / 7555 / 16416 ...）各自连上一次，
// 只看端口会当成多台设备，然后多个线程抢着点同一块屏幕 ——
// 表现就是"菜单刚打开就被关掉"。这里用设备自己的身份去重。
// android_id 每个实例独立、serialno 更通用，两个一起用。
std::string device_identity(const std::string& host) {
    try {
        AdbClient probe(host);
        std::string aid = trim(probe.shell("settings get secure android_id"));
        std::string sn = trim(probe.shell("getprop ro.serialno"));
        if (aid == "null") aid.clear();
        if (!aid.empty() || !sn.empty()) return aid + "|" + sn;
    } catch (...) {
    }
    return host;  // 拿不到就退回端口，至少不会崩
}

// adb 的设备列表（USB 真机 / emulator-5554 / 已经连上的网络设备）
// 加上本地模拟器常见端口的 connect 扫描。
std::vector<std::string> discover_serials() {
    std::vector<std::string> serials;
    std::set<std::string> seen;

    try {
        for (const std::string& s : AdbClient::list_devices()) {
            if (seen.insert(s).second) serials.push_back(s);
        }
    } catch (const std::exception& e) {
        std::cerr << "[ADB] devices: " << e.what() << std::endl;
    }

    // 5555~5599：BlueStacks / LDPlayer / MuMu(旧) 常用
    // 16384/16416：MuMu 12 默认；7555：MuMu 6；21503：MEmu；62001/62025：Nox
    std::vector<int> ports;
    for (int port = 5555; port <= 5599; port += 2) ports.push_back(port);
    for (int port : {16384, 16416, 7555, 21503, 62001, 62025}) ports.push_back(port);

    std::vector<std::future<std::pair<std::string, bool>>> futures;
    for (int port : ports) {
        futures.push_back(std::async(std::launch::async, [port]() {
            std::string host = "127.0.0.1:" + std::to_string(port);
            try {
                std::string result = AdbClient::connect(host);
                bool ok = result.find("connected") != std::string::npos;
                return std::make_pair(host, ok);
            } catch (...) {
                return std::make_pair(host, false);
            }
        }));
    }

    for (auto& f : futures) {
        auto [host, ok] = f.get();
        if (ok && seen.insert(host).second) serials.push_back(host);
    }
    return serials;
}

void show_no_devices() {
#if defined(_WIN32)
    MessageBox(nullptr,
               L"No devices found.\n\n"
               L"Check that the emulator is running and ADB is enabled,\n"
               L"then look at the console output for details.",
               L"CRBot", MB_OK | MB_ICONWARNING);
#else
    std::cerr << "[CRBOT] 没有找到设备。检查：\n"
                 "  1. 模拟器/真机是否已启动，并打开了 ADB 调试；\n"
                 "  2. `adb devices` 是否能列出设备；\n"
                 "  3. 分辨率必须是 720x1280"
                 "（真机可用 `adb shell wm size 720x1280` 临时改）。\n";
#endif
}

void show_fatal(const std::string& what) {
#if defined(_WIN32)
    MessageBoxA(nullptr, what.c_str(), "Fatal Error", MB_OK | MB_ICONERROR);
#else
    std::cerr << "\n[CRBOT] 致命错误：" << what << std::endl;
    std::cerr << "[CRBOT] 详细日志见 crbot.log" << std::endl;
#endif
}

int crbot_main() {
#if defined(_WIN32) && !defined(NDEBUG)
    // 只有 Debug 版才开控制台；Release 版不留终端窗口
    AllocConsole();
    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);
    setvbuf(stdout, nullptr, _IONBF, 0);
    SetConsoleOutputCP(CP_UTF8);  // 中文日志别变乱码
#endif

    // 控制台（如果有）和 crbot.log 双写
    if (start_log_tee("crbot.log")) {
#if defined(_WIN32) && !defined(NDEBUG)
        std::cout << "[LOG] writing to crbot.log" << std::endl;
#endif
    }
    std::cout << "[CRBOT] version " CRBOT_VERSION << std::endl;
    // GPL-3.0 §5d：交互式程序要给出法律声明
    std::cout << "[CRBOT] Copyright (C) 2026 CRBot contributors\n"
                 "[CRBOT] GPL-3.0-or-later, absolutely no warranty\n"
                 "[CRBOT] full terms in LICENSE; source: this project's repository"
              << std::endl;

    try {
        // 0. 先把 adb 守护进程拉起来（起不来的话这里就会报错，见控制台）
        AdbClient::start_server();

        // 1. 设备发现：adb devices + 本地模拟器端口扫描
        std::list<DeviceState> state_objs;
        std::vector<DeviceState*> state_ptrs;
        std::set<std::string> seen;

        for (const std::string& serial : discover_serials()) {
            const std::string id = device_identity(serial);
            if (!seen.insert(id).second) {
                std::cout << "[DEDUP] " << serial << " 与已连接的设备是同一台（serialno="
                          << id << "），跳过" << std::endl;
                continue;
            }
            std::cout << "[CONNECT] " << serial << "  (serialno=" << id << ")" << std::endl;
            state_objs.emplace_back(serial, "Connected");
        }

        if (state_objs.empty()) {
            show_no_devices();
            return 0;
        }

        for (auto& s : state_objs) state_ptrs.push_back(&s);

        // 2. 启动所有 BotRunner 线程
        std::list<BotRunner> runners;
        std::vector<BotRunner*> runner_ptrs;
        for (auto& s : state_objs) {
            runners.emplace_back(s);
            runner_ptrs.push_back(&runners.back());
        }
        for (auto* r : runner_ptrs) r->start();

        // 3. 监视线程：界面只登记请求，真正的 stop/join/start 在这里做。
        //    界面线程绝不能阻塞在 join() 上（机器人可能正卡在 adb 调用里）。
        std::atomic<bool> quitting{false};
        std::thread supervisor([&] {
            while (!quitting) {
                for (size_t i = 0; i < runner_ptrs.size() && i < state_ptrs.size(); ++i) {
                    int req = 0;
                    {
                        std::lock_guard<std::mutex> lk(state_ptrs[i]->mtx);
                        req = state_ptrs[i]->request;
                        state_ptrs[i]->request = 0;
                    }
                    if (req == 2) {
                        runner_ptrs[i]->abort();
                    } else if (req == 1) {
                        runner_ptrs[i]->restart();
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });

        // 4. 控制界面：Windows = Win32 窗口，macOS = 终端面板
#if defined(_WIN32)
        GuiDashboard gui(GetModuleHandle(nullptr), state_ptrs);
        gui.run();
#else
        ConsoleDashboard dashboard(state_ptrs);
        dashboard.run();
#endif

        quitting = true;
        supervisor.join();

        // 5. 退出：停止所有线程
        for (auto& r : runners) r.stop();
        for (auto& r : runners) r.join();

    } catch (const std::exception& e) {
        show_fatal(e.what());
        return 1;
    }
    return 0;
}

}  // namespace

#if defined(_WIN32)
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return crbot_main(); }
#else
int main() { return crbot_main(); }
#endif
