#include "bot_runner.h"
#include "battler.h"
#include "device.h"
#include "cr_game.h"
#include "navigator.h"

#include <chrono>
#include <cstdio>
#include <iostream>
#include <thread>

namespace {

constexpr int kMaxConsecutiveFailures = 3;
constexpr int kRetryBackoffMs = 3000;

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// 登记/注销"当前在用的 adb 客户端"，保证 abort() 不会碰到已析构的 Device
struct AdbLink {
    std::mutex& mtx;
    AdbClient*& slot;
    AdbLink(std::mutex& m, AdbClient*& s) : mtx(m), slot(s) {}
    ~AdbLink() {
        std::lock_guard<std::mutex> lk(mtx);
        slot = nullptr;
    }
};

}  // namespace

BotRunner::BotRunner(DeviceState& state) : state_(state) {}

BotRunner::~BotRunner() {
    stop();
    join();
}

void BotRunner::start() {
#if defined(__APPLE__)
    state_.running = true;
#endif
    thread_ = std::make_unique<std::thread>(&BotRunner::run, this);
}

void BotRunner::stop() {
    stop_ = true;
    // 如果在等待用户输入，唤醒它
    {
        std::lock_guard<std::mutex> lock(state_.mtx);
        state_.user_response = 2;  // disconnect
    }
    state_.cv.notify_all();

    // 一起打断正在执行的 adb 命令：否则 join() 要等截图那个 60 秒超时，
    // 退出程序时看起来就是"卡住不动"。
    std::lock_guard<std::mutex> lk(adb_mtx_);
    if (adb_) adb_->cancel();
}

void BotRunner::join() {
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

// 玩家人工介入之后，从最开始重新跑一遍（连设备 → 起游戏 → 等登录 → 选模式 → 打）
void BotRunner::restart() {
    if (restarting_.exchange(true)) return;  // 已经在重启了

    std::cerr << "[BOT] " << state_.serial << " restart requested" << std::endl;

    state_.set_status("Restarting...");
    stop();

    // 先打断正在跑的 adb 命令，否则 join() 可能卡到 60 秒超时
    {
        std::lock_guard<std::mutex> lk(adb_mtx_);
        if (adb_) adb_->cancel();
    }
    join();  // 现在最多等它跑完当前这一步（通常 <1 秒）

    stop_ = false;
    {
        std::lock_guard<std::mutex> lock(state_.mtx);
        state_.user_response = 0;
        state_.awaiting_input = false;
        state_.error = false;
        state_.running = true;  // 先占位，避免按钮被连点
    }

    start();
    restarting_ = false;
}

// 立即打断：不等 adb 命令超时，直接杀掉子进程；线程会立刻从阻塞的读取里返回
void BotRunner::abort() {
    std::cerr << "[BOT] " << state_.serial << " abort requested" << std::endl;

    stop_ = true;
    {
        std::lock_guard<std::mutex> lock(state_.mtx);
        state_.user_response = 2;
    }
    state_.cv.notify_all();
    state_.set_status("Aborted");

    std::lock_guard<std::mutex> lk(adb_mtx_);
    if (adb_) adb_->cancel();
}

void BotRunner::run() {
    state_.running = true;
    state_.set_status("Initializing...");

    try {
        // ---- 一次性初始化：设备 → 启动游戏 → 登录 → 选经典1v1 ----
#if defined(__APPLE__)
        // Register cancellation before the first screenshot, which can itself hang.
        Device device(state_.serial, false);
#else
        Device device(state_.serial);
#endif
        AdbLink link(adb_mtx_, adb_);
        {
            std::lock_guard<std::mutex> lk(adb_mtx_);
            adb_ = &device.adb();
#if defined(__APPLE__)
            if (stop_) adb_->cancel();
#endif
        }
#if defined(__APPLE__)
        device.initialize();
#endif
        state_.set_status("Device OK - " + std::to_string(device.width()) +
                          "x" + std::to_string(device.height()));

        CRGame game(device);
        game.ensure_running(&stop_);
        state_.set_status("Game launched");

        if (!game.wait_for_main_menu(&state_, 60, &stop_)) {
#if defined(__APPLE__)
            state_.set_status(stop_ ? "Stopped" : "Disconnected by user");
#else
            state_.set_status("Disconnected by user");
#endif
            state_.running = false;
            return;
        }

        state_.set_status("Navigating to 1v1...");
        Navigator nav(device);
        bool navigated = false;
        for (int attempt = 1; attempt <= 3 && !stop_ && !navigated; ++attempt) {
            if (attempt > 1) {
                state_.set_status("Navigation retry " + std::to_string(attempt) + "/3");
                sleep_ms(3000);
            }
            navigated = nav.navigate_to_1v1();
        }
        if (!navigated) {
#if defined(__APPLE__)
            state_.set_status(stop_ ? "Stopped" : "Navigation failed");
            state_.error = !stop_;
#else
            state_.set_status("Navigation failed");
            state_.error = true;
#endif
            state_.running = false;
            return;
        }

        // 对局内循环通过这个回调往外播报，Battler 本身不认识 DeviceState
        auto sink = [this](const BattleTelemetry& t) {
            switch (t.phase) {
                case BattlePhase::Starting:
                    state_.set_status("Matchmaking...");
                    break;
                case BattlePhase::Running: {
                    char hand[5];
                    for (int i = 0; i < 4; ++i) {
                        hand[i] = t.hand[i] == CardState::Ready        ? 'R'
                                  : t.hand[i] == CardState::Unaffordable ? 'u'
                                  : t.hand[i] == CardState::Empty        ? '-'
                                                                         : '?';
                    }
                    hand[4] = '\0';
                    char buf[112];
                    std::snprintf(buf, sizeof(buf),
                                  "Battle %llds: elixir %d/10 hand %s (played %d)",
                                  t.elapsed_ms / 1000, t.elixir, hand, t.played);
                    state_.set_status(buf);
                    break;
                }
                case BattlePhase::Finished:
                    state_.set_status("Battle finished");
                    break;
            }
        };

        // ---- 对局外循环：开一局 → 交给 Battler → 收一局 ----
        // 这里只关心"下一局能不能开起来"，对局里发生了什么一概不知道。
        int failures = 0;
        while (!stop_) {
            state_.set_status("Playing...");
            if (!nav.start_match()) {
                if (stop_) break;
                if (++failures >= kMaxConsecutiveFailures) {
                    state_.set_status("Cannot start match - giving up");
                    state_.error = true;
                    break;
                }
                state_.set_status("Start failed, retrying...");
                sleep_ms(kRetryBackoffMs);
                continue;
            }
            failures = 0;

            // 对局内循环：进场（圣水条出现）→ 打 → 出场（圣水条消失）
            {
                Battler battle(device, sink, stop_);
                if (!battle.wait_for_start()) {
                    std::cerr << "[BOT] battle did not start, recovering" << std::endl;
                } else {
                    battle.run();
                }
            }
            if (stop_) break;

            // 对局外收尾：结算界面点掉回主界面。失败也退避重试，不直接杀线程。
            if (!nav.finish_match()) {
                if (++failures >= kMaxConsecutiveFailures) {
                    state_.set_status("Cannot return to main menu - giving up");
                    state_.error = true;
                    break;
                }
                state_.set_status("Stuck in result screen, retrying...");
                sleep_ms(kRetryBackoffMs);
            } else {
                failures = 0;
            }
        }

    } catch (const std::exception& e) {
#if defined(__APPLE__)
        state_.set_status(stop_ ? "Stopped" : "ERROR: " + std::string(e.what()));
        state_.error = !stop_;
#else
        state_.set_status("ERROR: " + std::string(e.what()));
        state_.error = true;
#endif
    }

#if defined(__APPLE__)
    if (stop_) state_.set_status("Stopped");
#endif
    state_.running = false;
}
