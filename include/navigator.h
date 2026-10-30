#pragma once
#include "device.h"
#include "ui_buttons.h"
#include <random>

class Navigator {
public:
    explicit Navigator(Device& device);

    // 从主界面导航到经典1v1对战
    bool navigate_to_1v1();

    // ---- 对局外：两个原子操作，由 BotRunner 串联 ----

    // 在主界面点"对战"发起匹配
    bool start_match();

    // 结算 / 每日奖励 / 宝箱：一路点回主界面
    bool finish_match();

private:
    void tap_jitter(int x, int y);
    Image capture();                       // 抓一帧（失败返回空图）
    bool at_main_menu_in(const Image& img);
    bool at_main_menu();                   // 内部自己抓一帧
    void save_debug_frame();               // 把当前画面存成 png，排查用

    // 等画面停止变化（列表滑动的惯性结束）。返回 true = 已静止。
    bool wait_until_still(int max_ms = 2000, double eps = 2.5);

    Device& device_;
    std::mt19937 rng_;
    std::uniform_real_distribution<double> jitter_{-10.0, 10.0};
};
