#include "ui_buttons.h"
#include "platform.h"

#include <cstdio>
#include <filesystem>
#include <utility>

namespace fs = std::filesystem;

namespace {

// 模板跟着可执行文件走：build 目录下是 assets/templates（CMake 构建后复制），
// 打包版是 exe 旁边；开发时再退回源码树里的 assets/templates。
fs::path exe_assets_dir() {
    std::error_code ec;
    const std::string base = platform::exe_dir();
    if (!base.empty()) {
        for (const char* rel : {
#if defined(__APPLE__)
                                "../Resources/assets/templates",
#endif
                                "assets/templates", "../assets/templates",
                                "../../assets/templates"}) {
            const fs::path cand = fs::path(base) / rel;
            if (fs::is_directory(cand, ec)) return cand;
        }
    }
#ifdef CRBOT_SOURCE_ASSETS
    {
        const fs::path cand(CRBOT_SOURCE_ASSETS);
        if (fs::is_directory(cand, ec)) return cand;
    }
#endif
    return {};
}

// 每个按钮：一组外观模板 + 搜索范围（720x1280 实测，与模板尺寸无关）
struct Spec {
    Btn btn;
    const char* files[4];  // 最多 4 个外观，nullptr 结束
    Rect roi;
};

const Spec kSpecs[] = {
    // 主界面底部黄色「对战」：
    //   普通版 / 带"每日额外奖励"版 / 带皇冠计数与倒计时版（btn_battle3，用掩膜忽略动态数字）
    {Btn::Battle,
     {"btn_battle.png", "btn_battle2.png", "btn_battle3.png", nullptr},
     Rect{210, 900, 310, 220}},

    // 模式菜单里的「经典1对1」：位置随滚动变化 → 全屏搜
    {Btn::Mode1v1, {"btn_mode_1v1.png", nullptr, nullptr, nullptr}, Rect{}},

    // 结算页蓝色「确定」
    {Btn::Ok, {"btn_ok.png", nullptr, nullptr, nullptr}, Rect{255, 1040, 220, 140}},

    // 主界面上显示"当前模式"的图标：命中说明已经在经典1对1。
    // 图标尺寸小，搜索范围就限定在模式按钮附近（固定位置 570,1000 一带）。
    {Btn::ModeIcon, {"btn_mode_icon.png", nullptr, nullptr, nullptr}, Rect{490, 920, 180, 180}},
};

}  // namespace

UiButtons& UiButtons::shared() {
    static UiButtons inst = [] {
        UiButtons b;
        std::string err;
        if (!b.load(&err)) {
            std::fprintf(stderr, "[UI] button templates unavailable: %s\n", err.c_str());
        }
        return b;
    }();
    return inst;
}

bool UiButtons::load(std::string* err) {
    const fs::path dir = exe_assets_dir();
    if (dir.empty()) {
        if (err) *err = "assets/templates not found next to executable";
        return false;
    }

    for (const Spec& s : kSpecs) {
        Entry& e = entries_[static_cast<int>(s.btn)];
        e.roi = s.roi;

        for (const char* f : s.files) {
            if (!f) break;
            const fs::path path = dir / f;
            std::string perr;
            Template t = Template::load(path, &perr);
            if (!t.valid()) {
                std::fprintf(stderr, "[UI] cannot load %s: %s\n", path.string().c_str(),
                             perr.c_str());
                continue;
            }
            std::fprintf(stderr, "[UI] loaded %s (%dx%d)\n", path.string().c_str(), t.width(),
                         t.height());
            e.tpls.push_back(std::move(t));
            ++loaded_;
        }
    }
    return loaded_ > 0;
}

MatchResult UiButtons::find(const Image& img, Btn b, double min_score) const {
    MatchResult best;
    const Entry& e = entries_[static_cast<int>(b)];
    if (img.empty() || e.tpls.empty()) return best;

    for (const Template& tpl : e.tpls) {
        MatchResult m = match_template(img, tpl, e.roi, 0.0);
        if (m.score > best.score) best = m;
    }
    best.found = best.score >= min_score;
    return best;
}
