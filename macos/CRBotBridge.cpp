#include "CRBotBridge.h"
#include "crbot_version.h"
#include "bot_runner.h"
#include "log_tee.h"
#include "platform.h"
#include "proc.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::string json(const std::string& value) {
    std::string out = "\"";
    for (unsigned char c : value) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 32) {
                    const char* hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else out += static_cast<char>(c);
        }
    }
    return out + '"';
}

std::string trim(std::string s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    return s.substr(begin, s.find_last_not_of(" \t\r\n") - begin + 1);
}

struct Entry {
    DeviceState state;
    BotRunner runner;
    std::string identity;
    std::atomic<bool> pending{false};
    std::atomic<bool> desired_running{false};
    Entry(std::string serial, std::string id)
        : state(std::move(serial), "Ready"), runner(state), identity(std::move(id)) {}
};
}  // namespace

struct CRBotBackend final : LogSink {
    std::mutex mutex;
    std::vector<std::unique_ptr<Entry>> entries;
    std::string message = "准备扫描设备";
    std::string error;
    std::string log_path;
    std::atomic<bool> discovering{false}, quitting{false}, finished{false};
    CancelState discovery_cancel;
    std::thread discovery, supervisor;

    std::mutex log_mutex;
    std::deque<std::pair<unsigned long long, std::string>> logs;
    std::string partial;
    unsigned long long next_log = 1;

    explicit CRBotBackend(const std::string& directory) {
        log_path = (std::filesystem::path(directory) / "crbot.log").string();
        set_log_sink(this);
        if (!start_log_tee(log_path.c_str())) {
            set_log_sink(nullptr);
            throw std::runtime_error("无法创建日志文件：" + log_path);
        }
        std::cout << "[CRBOT] version " CRBOT_VERSION " · macOS SwiftUI\n"
                     "[CRBOT] Copyright (C) 2026 CRBot contributors\n"
                     "[CRBOT] GPL-3.0-or-later, absolutely no warranty\n" << std::flush;
        supervisor = std::thread([this] { supervise(); });
    }

    ~CRBotBackend() override {
        shutdown();
        if (supervisor.joinable()) supervisor.join();
        set_log_sink(nullptr);
    }

    void on_log(const char* data, size_t size) override {
        std::lock_guard lock(log_mutex);
        partial.append(data, size);
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            logs.emplace_back(next_log++, partial.substr(0, pos));
            partial.erase(0, pos + 1);
            if (logs.size() > 2000) logs.pop_front();
        }
        // Malformed/no-newline output must not grow without bound. Keep valid
        // UTF-8 boundaries when splitting a long line.
        if (partial.size() > 16384) {
            size_t end = 16384;
            while (end && (static_cast<unsigned char>(partial[end]) & 0xc0) == 0x80) --end;
            logs.emplace_back(next_log++, partial.substr(0, end));
            partial.erase(0, end);
            if (logs.size() > 2000) logs.pop_front();
        }
    }

    std::vector<Entry*> all_entries() {
        std::lock_guard lock(mutex);
        std::vector<Entry*> out;
        for (auto& entry : entries) out.push_back(entry.get());
        return out; // Entries are never removed until shutdown has joined.
    }

    void supervise() {
        while (!quitting) {
            for (auto* entry : all_entries()) {
                int request;
                {
                    std::lock_guard lock(entry->state.mtx);
                    request = std::exchange(entry->state.request, 0);
                }
                if (request == 1 && !quitting) {
                    try {
                        entry->runner.restart();
                        // Stop/quit can arrive while restart is joining an old
                        // worker. Do not let that restart undo the user's stop.
                        if (!entry->desired_running || quitting) entry->runner.stop();
                    }
                    catch (const std::exception& e) {
                        entry->state.set_status(e.what());
                        entry->state.error = true;
                        entry->state.running = false;
                    }
                    entry->pending = false;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (discovery.joinable()) discovery.join();
        const auto current = all_entries();
        for (auto* entry : current) entry->runner.stop();
        for (auto* entry : current) entry->runner.join();
        finished = true;
    }

    std::string adb(const std::string& path, std::vector<std::string> args, int timeout = 5000) {
        args.insert(args.begin(), path);
        const auto result = run_process(args, timeout, &discovery_cancel);
        const std::string output(result.out.begin(), result.out.end());
        if (result.timed_out) throw std::runtime_error("ADB 操作超时");
        if (result.exit_code != 0) throw std::runtime_error(trim(output).empty() ? "ADB 操作失败" : trim(output));
        return output;
    }

    bool cancelled() {
        std::lock_guard lock(discovery_cancel.mtx);
        return discovery_cancel.cancelled;
    }

    void add_device(const std::string& path, const std::string& serial) {
        // A rescan must not compete with the bot's persistent shell.
        {
            std::lock_guard lock(mutex);
            for (const auto& entry : entries) if (entry->state.serial == serial) return;
        }
        auto aid = trim(adb(path, {"-s", serial, "shell", "settings", "get", "secure", "android_id"}));
        const auto sn = trim(adb(path, {"-s", serial, "shell", "getprop", "ro.serialno"}));
        if (aid == "null") aid.clear();
        const auto identity = aid.empty() && sn.empty() ? serial : aid + "|" + sn;
        if (cancelled()) return;
        std::lock_guard lock(mutex);
        for (const auto& entry : entries) {
            if (entry->identity == identity || entry->state.serial == serial) {
                std::cout << "[DEDUP] " << serial << " -> " << entry->state.serial << std::endl;
                return;
            }
        }
        entries.push_back(std::make_unique<Entry>(serial, identity));
        std::cout << "[CONNECT] " << serial << " ready" << std::endl;
    }

    void discover(const std::string& endpoint) {
        if (quitting || discovering.exchange(true)) return;
        if (discovery.joinable()) discovery.join(); // Finished scan only.
        {
            std::lock_guard lock(discovery_cancel.mtx);
            discovery_cancel.cancelled = false;
        }
        {
            std::lock_guard lock(mutex);
            error.clear();
            message = endpoint.empty() ? "正在扫描 USB 与本地模拟器…" : "正在连接 " + endpoint;
        }
        discovery = std::thread([this, endpoint] {
            try {
                const auto path = platform::resolve_adb();
                if (path.empty()) throw std::runtime_error(
                    "未找到 ADB。请安装 Android Platform Tools，或设置 CRBOT_ADB 后重新打开应用。安装后也可重新扫描。\n"
                    "brew install --cask android-platform-tools");
                std::cout << "[ADB] using " << path << std::endl;
                adb(path, {"start-server"}, 15000);
                if (!endpoint.empty()) {
                    if (endpoint.front() == '-' || endpoint.find_first_of(" \t\r\n") != std::string::npos ||
                        endpoint.find(':') == std::string::npos)
                        throw std::runtime_error("请输入 host:port，例如 127.0.0.1:5555");
                    const auto result = adb(path, {"connect", endpoint}, 10000);
                    std::cout << "[ADB] " << result << std::flush;
                    if (result.find("connected to") == std::string::npos)
                        throw std::runtime_error(trim(result));
                }
                // List first so USB devices appear while local ports are scanned.
                auto list = [&] {
                    std::istringstream stream(adb(path, {"devices"}));
                    std::string line;
                    while (std::getline(stream, line) && !cancelled()) {
                        std::istringstream row(line);
                        std::string serial, state;
                        row >> serial >> state;
                        if (state == "device") {
                            try { add_device(path, serial); }
                            catch (const std::exception& e) {
                                if (cancelled()) throw;
                                std::cerr << "[DISCOVERY] " << serial << ": " << e.what() << std::endl;
                            }
                        } else if (state == "unauthorized" || state == "offline") {
                            std::cerr << "[ADB] " << serial << " " << state
                                      << " — 请检查设备授权或重新连接" << std::endl;
                        }
                    }
                };
                list();
                if (endpoint.empty()) {
                    std::vector<int> ports;
                    for (int port = 5555; port <= 5599; port += 2) ports.push_back(port);
                    for (int port : {16384, 16416, 7555, 21503, 62001, 62025}) ports.push_back(port);
                    for (int port : ports) {
                        if (cancelled()) break;
                        try { adb(path, {"connect", "127.0.0.1:" + std::to_string(port)}, 800); }
                        catch (...) { if (cancelled()) break; }
                    }
                    if (!cancelled()) list();
                }
                std::lock_guard lock(mutex);
                message = cancelled() ? "扫描已取消" : entries.empty()
                    ? "没有发现可用设备。请开启 USB 调试、允许设备授权，或手动连接 host:port。"
                    : "扫描完成 · " + std::to_string(entries.size()) + " 台设备";
            } catch (const std::exception& e) {
                std::lock_guard lock(mutex);
                if (cancelled()) message = "扫描已取消";
                else {
                    error = e.what();
                    message = "连接失败，可以重试";
                    std::cerr << "[ERROR] " << error << std::endl;
                }
            }
            discovering = false;
        });
    }

    void action(const std::string& serial, int action) {
        if (quitting) return;
        for (auto* entry : all_entries()) {
            if (entry->state.serial != serial) continue;
            if (action == 1) {
                if (entry->pending.exchange(true)) return;
                entry->desired_running = true;
                std::lock_guard lock(entry->state.mtx);
                entry->state.request = 1;
            } else if (action == 2 || action == 4) {
                entry->desired_running = false;
                {
                    std::lock_guard lock(entry->state.mtx);
                    entry->state.request = 0;
                }
                entry->runner.stop();
                entry->state.set_status(action == 4 ? "Disconnected by user" : "Stopping...");
                if (!entry->state.running) entry->state.set_status(action == 4 ? "Disconnected by user" : "Stopped");
                entry->pending = false;
            } else if (action == 3) {
                std::lock_guard lock(entry->state.mtx);
                if (entry->state.awaiting_input) {
                    entry->state.user_response = 1;
                    entry->state.cv.notify_all();
                }
            }
        }
    }

    void stop_all() {
        for (auto* entry : all_entries()) action(entry->state.serial, 2);
    }

    void shutdown() {
        if (quitting.exchange(true)) return;
        cancel_process(discovery_cancel);
        for (auto* entry : all_entries()) entry->runner.stop();
    }

    std::string snapshot() {
        std::ostringstream out;
        out << "{\"discovering\":" << (discovering ? "true" : "false")
            << ",\"logPath\":" << json(log_path);
        {
            std::lock_guard lock(mutex);
            out << ",\"message\":" << json(message) << ",\"error\":" << json(error) << ",\"devices\":[";
            bool first = true;
            for (const auto& entry : entries) {
                if (!first) out << ',';
                first = false;
                auto& state = entry->state;
                std::lock_guard state_lock(state.mtx);
                out << "{\"serial\":" << json(state.serial) << ",\"status\":" << json(state.status)
                    << ",\"running\":" << (state.running ? "true" : "false")
                    << ",\"error\":" << (state.error ? "true" : "false")
                    << ",\"awaitingInput\":" << (state.awaiting_input ? "true" : "false")
                    << ",\"pending\":" << (entry->pending ? "true" : "false") << '}';
            }
            out << ']';
        }
        {
            std::lock_guard lock(log_mutex);
            out << ",\"logs\":[";
            bool first = true;
            for (const auto& [id, line] : logs) {
                if (!first) out << ',';
                first = false;
                out << "{\"id\":" << id << ",\"text\":" << json(line) << '}';
            }
            out << "]}";
        }
        return out.str();
    }
};

extern "C" {
CRBotBackend* crbot_create(const char* directory) {
    try { return new CRBotBackend(directory ? directory : "."); }
    catch (...) { return nullptr; }
}
void crbot_discover(CRBotBackend* backend, const char* endpoint) {
    if (backend) backend->discover(endpoint ? endpoint : "");
}
void crbot_cancel_discovery(CRBotBackend* backend) {
    if (backend) cancel_process(backend->discovery_cancel);
}
void crbot_device_action(CRBotBackend* backend, const char* serial, int action) {
    if (backend && serial) backend->action(serial, action);
}
void crbot_stop_all(CRBotBackend* backend) { if (backend) backend->stop_all(); }
char* crbot_snapshot(CRBotBackend* backend) {
    if (!backend) return nullptr;
    try { return ::strdup(backend->snapshot().c_str()); }
    catch (...) { return nullptr; }
}
void crbot_free_string(char* value) { std::free(value); }
void crbot_clear_logs(CRBotBackend* backend) {
    if (!backend) return;
    std::lock_guard lock(backend->log_mutex);
    backend->logs.clear();
    backend->partial.clear();
}
void crbot_shutdown(CRBotBackend* backend) { if (backend) backend->shutdown(); }
int crbot_shutdown_complete(CRBotBackend* backend) { return !backend || backend->finished; }
void crbot_destroy(CRBotBackend* backend) { delete backend; }
}
