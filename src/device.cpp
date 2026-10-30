#include "device.h"
#include "png_io.h"
#include <iostream>

#if defined(__APPLE__)
Device::Device(std::string serial, bool initialize_now)
    : serial_(std::move(serial)), adb_(serial_) {
    if (initialize_now) initialize();
}

void Device::initialize() {
#else
Device::Device(std::string serial)
    : serial_(std::move(serial)), adb_(serial_) {
#endif
    auto raw = adb_.screenshot_raw();
    if (raw.size() < 12) {
        throw std::runtime_error(serial_ + ": screenshot too small (" +
                                 std::to_string(raw.size()) + " bytes)");
    }

    width_  = *reinterpret_cast<uint32_t*>(&raw[0]);
    height_ = *reinterpret_cast<uint32_t*>(&raw[4]);

    std::string model = adb_.shell("getprop ro.product.model");
    while (!model.empty() && (model.back() == '\n' || model.back() == '\r'))
        model.pop_back();

    std::cout << "[DEVICE] " << serial_
              << " | " << model
              << " | " << width_ << "x" << height_;

    if (width_ != REQUIRED_W || height_ != REQUIRED_H) {
        std::cout << " [REJECTED: need " << REQUIRED_W << "x" << REQUIRED_H << "]"
                  << std::endl;
        throw std::runtime_error(
            serial_ + ": resolution " + std::to_string(width_) + "x" + std::to_string(height_) +
            " != " + std::to_string(REQUIRED_W) + "x" + std::to_string(REQUIRED_H));
    }

    std::cout << " [OK]" << std::endl;
}

Image Device::screenshot() {
    // 截图优先走 PNG：一帧约 80KB，原始帧是 3.7MB（720x1280x4）。
    // 多开时瓶颈就是 adb 通道，传 PNG 能快好几倍，也不容易把某台拖到 60 秒超时。
    static thread_local bool prefer_png = true;

    if (prefer_png) {
        try {
            auto png = adb_.screenshot_png();
            std::string err;
            Image img;
            if (load_png_memory(png, img, &err) && !img.empty()) return img;
            std::cerr << "[DEVICE] " << serial_ << " png decode failed: " << err
                      << ", switching to raw" << std::endl;
            prefer_png = false;  // 解码失败是系统性的，别再重试
        } catch (const std::exception& e) {
            // 截图命令本身失败（超时等）只影响这一帧，下次继续用 PNG
            std::cerr << "[DEVICE] " << serial_ << " png capture failed: " << e.what()
                      << std::endl;
        }
    }

    auto raw = adb_.screenshot_raw();
    std::string err;
    Image img = Image::from_screencap(raw, &err);
    if (img.empty()) {
        throw std::runtime_error(serial_ + ": bad screencap (" + err + ")");
    }
    return img;
}
