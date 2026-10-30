#include "template_match.h"
#include "png_io.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace {

inline int luma(const uint8_t* p) {
    // 0.299R + 0.587G + 0.114B 的整数近似
    return (p[0] * 76 + p[1] * 150 + p[2] * 29) >> 8;
}

// 归一化相关系数（对亮度/对比度变化不敏感）。
// 掩膜里的像素直接跳过：动态文字 / 红点角标不影响分数。
double ncc_at(const Image& img, const Template& tpl, int ox, int oy, int step) {
    const int tw = tpl.width(), th = tpl.height();
    const auto& g = tpl.gray();
    const auto& mask = tpl.mask();
    const bool has_mask = !mask.empty();

    double ss = 0, ss2 = 0, st = 0, st2 = 0, sst = 0;
    int n = 0;
    for (int y = 0; y < th; y += step) {
        const uint8_t* row = img.at(ox, oy + y);
        if (!row) return 0.0;
        const int trow = y * tw;
        for (int x = 0; x < tw; x += step) {
            if (has_mask && mask[trow + x] == 0) continue;
            const double s = luma(row + x * 4);
            const double t = g[trow + x];
            ss += s;
            ss2 += s * s;
            st += t;
            st2 += t * t;
            sst += s * t;
            ++n;
        }
    }
    if (n < 8) return 0.0;

    const double inv = 1.0 / n;
    const double mean_s = ss * inv, mean_t = st * inv;
    const double var_s = ss2 * inv - mean_s * mean_s;
    const double var_t = st2 * inv - mean_t * mean_t;
    if (var_s < 1e-6 || var_t < 1e-6) return 0.0;
    return (sst * inv - mean_s * mean_t) / std::sqrt(var_s * var_t);
}

}  // namespace

Template Template::from_image(const Image& img) {
    Template t;
    if (img.empty()) return t;
    t.w_ = img.width();
    t.h_ = img.height();
    t.gray_.resize(static_cast<size_t>(t.w_) * t.h_);
    for (int y = 0; y < t.h_; ++y) {
        for (int x = 0; x < t.w_; ++x) {
            t.gray_[static_cast<size_t>(y) * t.w_ + x] = static_cast<uint8_t>(luma(img.at(x, y)));
        }
    }
    return t;
}

Template Template::load(const std::filesystem::path& png, std::string* err) {
    Image img;
    if (!load_png(png, img, err)) return {};
    Template t = from_image(img);

    // 可选的掩膜：<png>.mask，每行 "x y w h"（模板坐标）
    std::error_code ec;
    std::filesystem::path mask_path = png;
    mask_path += ".mask";
    if (std::filesystem::is_regular_file(mask_path, ec)) {
        std::FILE* f = std::fopen(mask_path.string().c_str(), "rb");
        if (f) {
            char line[128];
            int rects = 0;
            while (std::fgets(line, sizeof(line), f)) {
                const char* p = line;
                while (*p == ' ' || *p == '\t') ++p;
                if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
                int x = 0, y = 0, w = 0, h = 0;
                if (std::sscanf(p, "%d %d %d %d", &x, &y, &w, &h) == 4) {
                    t.add_mask_rect(Rect{x, y, w, h});
                    ++rects;
                }
            }
            std::fclose(f);
            std::fprintf(stderr, "[TPL] %s mask: %d rect(s), %.0f%% pixels ignored\n",
                         png.filename().string().c_str(), rects, t.masked_ratio() * 100.0);
        }
    }
    return t;
}

void Template::add_mask_rect(const Rect& r) {
    if (w_ <= 0 || h_ <= 0) return;
    if (mask_.empty()) mask_.assign(static_cast<size_t>(w_) * h_, 1);
    const int x0 = std::max(0, r.x);
    const int y0 = std::max(0, r.y);
    const int x1 = std::min(w_, r.x + r.w);
    const int y1 = std::min(h_, r.y + r.h);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            mask_[static_cast<size_t>(y) * w_ + x] = 0;
        }
    }
}

double Template::masked_ratio() const {
    if (mask_.empty()) return 0.0;
    size_t ignored = 0;
    for (uint8_t v : mask_) {
        if (!v) ++ignored;
    }
    return static_cast<double>(ignored) / static_cast<double>(mask_.size());
}

MatchResult match_template(const Image& img, const Template& tpl, Rect roi,
                           double min_score) {
    MatchResult best;
    if (img.empty() || !tpl.valid()) return best;

    if (!roi.valid()) roi = Rect{0, 0, img.width(), img.height()};
    roi.x = std::max(0, roi.x);
    roi.y = std::max(0, roi.y);
    roi.w = std::min(roi.w, img.width() - roi.x);
    roi.h = std::min(roi.h, img.height() - roi.y);
    if (roi.w < tpl.width() || roi.h < tpl.height()) return best;

    const int max_x = roi.x + roi.w - tpl.width();
    const int max_y = roi.y + roi.h - tpl.height();
    const int step = tpl.sample_step();
    // 模板越大粗扫越稀，否则 Debug 构建下全屏搜索会卡几秒
    const int coarse = (tpl.width() * tpl.height() > 40000) ? 8 : 4;

    // 粗扫
    for (int y = roi.y; y <= max_y; y += coarse) {
        for (int x = roi.x; x <= max_x; x += coarse) {
            const double s = ncc_at(img, tpl, x, y, step);
            if (s > best.score) {
                best.score = s;
                best.rect = Rect{x, y, tpl.width(), tpl.height()};
            }
        }
    }
    if (best.score <= 0.0) return best;

    // 在粗扫最优点附近逐像素精修
    const int bx = best.rect.x, by = best.rect.y;
    for (int y = std::max(roi.y, by - coarse); y <= std::min(max_y, by + coarse); ++y) {
        for (int x = std::max(roi.x, bx - coarse); x <= std::min(max_x, bx + coarse); ++x) {
            const double s = ncc_at(img, tpl, x, y, step);
            if (s > best.score) {
                best.score = s;
                best.rect = Rect{x, y, tpl.width(), tpl.height()};
            }
        }
    }

    best.found = best.score >= min_score;
    return best;
}
