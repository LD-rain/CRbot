#pragma once
#include "adb_client.h"
#include "image.h"
#include <string>
#include <cstdint>
#include <stdexcept>

class Device {
public:
    static constexpr uint32_t REQUIRED_W = 720;
    static constexpr uint32_t REQUIRED_H = 1280;

    // 构造时连接并校验分辨率，不合格直接抛异常
#if defined(__APPLE__)
    explicit Device(std::string serial, bool initialize_now = true);
    void initialize();
#else
    explicit Device(std::string serial);
#endif

    const std::string& serial() const { return serial_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }

    AdbClient& adb() { return adb_; }

    // 抓一帧（RGBA）。失败抛异常。
    Image screenshot();

private:
    std::string serial_;
    uint32_t width_ = 0;
    uint32_t height_ = 0;
    AdbClient adb_;
};
