#include "navigator.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <utility>

namespace {

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

long long elapsed_ms(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t).count();
}

inline int luma8(const uint8_t* p) { return (p[0] * 76 + p[1] * 150 + p[2] * 29) >> 8; }

// 两帧的粗略差异（0~255，灰度平均绝对差）。
// 只看模式列表会占据的区域，避免背景里的轻微动画影响判断。
double frame_diff(const Image& a, const Image& b) {
    if (a.empty() || b.empty() || a.width() != b.width() || a.height() != b.height()) {
        return 255.0;
    }
    long long sum = 0;
    int n = 0;
    for (int y = 160; y < a.height() - 160; y += 8) {
        for (int x = 16; x < a.width() - 16; x += 8) {
            const uint8_t* pa = a.at(x, y);
            const uint8_t* pb = b.at(x, y);
            if (!pa || !pb) continue;
            sum += std::abs(luma8(pa) - luma8(pb));
            ++n;
        }
    }
    return n ? static_cast<double>(sum) / n : 255.0;
}

// 阈值：模板是从当前版本截的，正常命中在 0.9 以上；
// 0.72 是为了容忍闪光动画和抗锯齿差异。
constexpr double kMenuScore = 0.72;
constexpr double kOkScore = 0.70;
constexpr double kModeScore = 0.80;
// 模式图标带掩膜（忽略红点角标），两种状态应该都很高；给一点余量
constexpr double kModeIconScore = 0.82;

}  // namespace

Navigator::Navigator(Device& device)
    : device_(device),
      rng_(static_cast<unsigned>(
          std::chrono::steady_clock::now().time_since_epoch().count())) {}

void Navigator::tap_jitter(int x, int y) {
    int jx = x + static_cast<int>(jitter_(rng_));
    int jy = y + static_cast<int>(jitter_(rng_));
    device_.adb().tap(jx, jy);
}

Image Navigator::capture() {
    try {
        return device_.screenshot();
    } catch (const std::exception& e) {
        std::cerr << "[NAV] screenshot failed: " << e.what() << std::endl;
        return {};
    }
}

bool Navigator::at_main_menu_in(const Image& img) {
    if (img.empty()) return false;
    return UiButtons::shared().find(img, Btn::Battle, kMenuScore).found;
}

bool Navigator::at_main_menu() { return at_main_menu_in(capture()); }

bool Navigator::wait_until_still(int max_ms, double eps) {
    Image prev = capture();
    if (prev.empty()) return false;

    auto start = std::chrono::steady_clock::now();
    while (elapsed_ms(start) < max_ms) {
        sleep_ms(150);
        Image cur = capture();
        if (cur.empty()) continue;
        const double diff = frame_diff(prev, cur);
        if (diff <= eps) return true;
        prev = std::move(cur);
    }
    std::cerr << "[NAV] list still moving after " << elapsed_ms(start) << "ms" << std::endl;
    return false;
}

bool Navigator::navigate_to_1v1() {
    // 已经在经典1对1 就不用开菜单重选：
    // 主界面上"当前模式"的图标就是 1v1 的盾牌图标（右上角可能带红点角标）。
    {
        Image img = capture();
        if (!img.empty()) {
            MatchResult icon = UiButtons::shared().find(img, Btn::ModeIcon, kModeIconScore);
            if (icon.found) {
                std::cout << "[NAV] " << device_.serial() << " already in 1v1 (mode icon "
                          << icon.score << "), skipping mode menu" << std::endl;
                return true;
            }
        }
    }

    std::cout << "[NAV] " << device_.serial() << " opening mode menu..." << std::endl;

    // 模式菜单的开合按钮图标随当前模式变化（还会随版本改），没法做模板，
    // 只能按固定位置点。菜单项的位置会随滚动变化，所以每次都重新找。
    for (int attempt = 0; attempt < 3; ++attempt) {
        tap_jitter(570, 1000);
        sleep_ms(900);

        for (int scroll = 0; scroll < 4; ++scroll) {
            // 关键：先在列表**完全停稳**之后再截图。
            // 以前是滑动完固定等 700ms 就截图，松手瞬间列表还在惯性上滑，
            // 截到的是旧位置，于是点在"1v1 当时的位置"上，实际点到了下面的 2v2。
            wait_until_still();

            Image img = capture();
            if (!img.empty()) {
                MatchResult m = UiButtons::shared().find(img, Btn::Mode1v1, kModeScore);
                if (m.found) {
                    std::cout << "[NAV] 经典1对1 at (" << m.cx() << "," << m.cy()
                              << ") score " << m.score << std::endl;
                    tap_jitter(m.cx(), m.cy());
                    sleep_ms(1500);
                    if (at_main_menu()) {
                        std::cout << "[NAV] mode selected" << std::endl;
                        return true;
                    }
                }
            }
            // 没找到 / 选完没回主界面：滑动列表再看。
            // 用较长的 duration 慢慢拖，松手后不会带着惯性继续滑。
            device_.adb().swipe(360, 820, 360, 640, 700);
            sleep_ms(250);
        }

        tap_jitter(570, 1000);  // 关掉菜单，下一轮重开
        sleep_ms(700);
    }

    // 找不到也别卡住：游戏会记住上次的模式，多半已经是经典1对1
    std::cerr << "[NAV] 1v1 item not found; keeping current mode" << std::endl;
    return at_main_menu();
}

bool Navigator::start_match() {
    // 结算后的每日奖励、开箱、赛季拼图都是**动画**，可能要连着过好几层，
    // 所以这里最多等 120 秒，期间不断点空白处推进——不能等 20 秒就放弃。
    std::cerr << "[NAV] waiting for battle button..." << std::endl;

    auto start = std::chrono::steady_clock::now();
    int polls = 0;
    MatchResult btn;
    bool found = false;
    while (true) {
        Image img = capture();
        if (!img.empty()) {
            btn = UiButtons::shared().find(img, Btn::Battle, kMenuScore);
            if (btn.found) { found = true; break; }
        }
        if (elapsed_ms(start) > 120000) break;
        // 每 4 轮（约 2 秒）点一下空白处，把奖励动画推过去
        if (++polls % 4 == 0) tap_jitter(365, 500);
        sleep_ms(300);
    }
    if (!found) {
        std::cerr << "[NAV] battle button not found after " << elapsed_ms(start) / 1000 << "s"
                  << std::endl;
        save_debug_frame();
        return false;
    }

    // 点"对战"这一下有时会被游戏吞掉（加载中/转场），点完要验证：5 秒内按钮应消失
    for (int attempt = 1; attempt <= 3; ++attempt) {
        std::cerr << "[NAV] battle button at (" << btn.cx() << "," << btn.cy() << ") score "
                  << btn.score << ", tapping (attempt " << attempt << "/3)" << std::endl;
        tap_jitter(btn.cx(), btn.cy());

        // 验证：按钮消失 = 已进入匹配
        auto t1 = std::chrono::steady_clock::now();
        bool left = false;
        while (elapsed_ms(t1) < 5000) {
            sleep_ms(400);
            Image img = capture();
            if (img.empty()) continue;
            if (!UiButtons::shared().find(img, Btn::Battle, kMenuScore).found) {
                left = true;
                break;
            }
        }
        if (left) {
            std::cerr << "[NAV] matchmaking started" << std::endl;
            return true;
        }
        std::cerr << "[NAV] tap did not take effect, retry " << attempt << "/3" << std::endl;
        sleep_ms(600);
    }
    return false;
}

bool Navigator::finish_match() {
    if (at_main_menu()) {
        std::cerr << "[NAV] already at main menu" << std::endl;
        return true;
    }

    std::cerr << "[NAV] leaving battle result..." << std::endl;
    sleep_ms(1200);

    // 结算 → 每日首胜奖励 → 宝箱 → 主界面，中间可能好几层。
    // 奖励/拼图这类界面是**纯动画、没有可匹配的特征**，只能点空白处推进；
    // 所以这里只允许两个动作：
    //   1) 命中模板的「确定」→ 点它
    //   2) 否则点空白处 (365,500) 推进动画
    // 绝不能点 (360,1000) 之类的区域——那是底部「对战」按钮，会直接开下一局。
    for (int i = 0; i < 24; ++i) {
        Image img = capture();
        if (img.empty()) { sleep_ms(400); continue; }

        MatchResult battle = UiButtons::shared().find(img, Btn::Battle, kMenuScore);
        if (battle.found) {
            std::cerr << "[NAV] back to main menu" << std::endl;
            return true;
        }

        MatchResult ok = UiButtons::shared().find(img, Btn::Ok, kOkScore);
        std::fprintf(stderr, "[NAV] result iter %d: battle=%.2f ok=%.2f%s\n", i + 1,
                     battle.score, ok.score, ok.found ? " -> tap 确定" : " -> tap blank");

        if (ok.found) {
            tap_jitter(ok.cx(), ok.cy());
        } else {
            tap_jitter(365, 500);  // 空白处，只推进动画
        }

        // 前几轮就存一张现场图，方便看清是哪一层界面
        if (i == 2) save_debug_frame();

        sleep_ms(700);
    }

    std::cerr << "[NAV] could not confirm main menu" << std::endl;
    save_debug_frame();
    return false;
}

void Navigator::save_debug_frame() {
    Image img = capture();
    if (img.empty()) return;
    const char* path = "crbot_debug.png";
    if (img.save_png(path)) {
        std::cerr << "[NAV] saved current frame to " << path << std::endl;
    }
}
