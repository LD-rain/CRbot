#pragma once
#include "image.h"
#include <filesystem>
#include <string>
#include <vector>

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
    bool valid() const { return w > 0 && h > 0; }
    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }
};

// 一块按钮模板（灰度 + 尺寸），只在加载时算一次统计量。
//
// 掩膜（mask）：模板里有些像素每张截图都不一样——比如按钮上的
// "0/500" 计数、倒计时文案、图标右上角的红点角标（里面还有白色数字）。
// 这些区域在 <模板>.mask 里用矩形标出来，匹配时**完全不参与** NCC 计算，
// 于是同一份模板能同时匹配"计数为 0"和"计数为 500"、"有红点"和"没红点"。
class Template {
public:
    // png 旁边若存在 <png>.mask 会自动加载（纯文本，每行 "x y w h"，# 开头为注释）
    static Template load(const std::filesystem::path& png, std::string* err = nullptr);
    static Template from_image(const Image& img);

    int width() const { return w_; }
    int height() const { return h_; }
    bool valid() const { return w_ > 0 && h_ > 0 && !gray_.empty(); }

    const std::vector<uint8_t>& gray() const { return gray_; }

    bool has_mask() const { return !mask_.empty(); }
    const std::vector<uint8_t>& mask() const { return mask_; }

    // 把模板坐标系里的一个矩形排除掉（可以多次调用）
    void add_mask_rect(const Rect& r);
    // 掩膜像素占比（0~1），用来判断这块模板还剩多少有效信息
    double masked_ratio() const;

    // 采样步长：模板越大抽得越稀，保证单次匹配在 100ms 量级
    int sample_step() const { return (w_ * h_ > 12000) ? 3 : ((w_ * h_ > 3000) ? 2 : 1); }

private:
    int w_ = 0, h_ = 0;
    std::vector<uint8_t> gray_;
    std::vector<uint8_t> mask_;  // 空 = 全部参与；否则 0 = 忽略，1 = 参与
};

struct MatchResult {
    bool found = false;
    Rect rect;           // 命中的屏幕区域（左上角 + 模板尺寸）
    double score = 0.0;  // 归一化相关系数，1.0 = 完全一致

    int cx() const { return rect.cx(); }
    int cy() const { return rect.cy(); }
};

// 在 img 的 roi 区域内找 tpl。roi 传 {} 表示全屏。
// 先用大步长粗扫，再在最优位置附近逐像素精修。
MatchResult match_template(const Image& img, const Template& tpl, Rect roi,
                           double min_score = 0.80);
