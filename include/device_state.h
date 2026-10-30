#pragma once
#include <string>
#include <mutex>
#include <condition_variable>
#if defined(__APPLE__)
#include <atomic>
#endif

struct DeviceState {
    DeviceState() = default;
    DeviceState(std::string serial_, std::string status_)
        : serial(std::move(serial_)), status(std::move(status_)) {}

    std::string serial;
    std::string status;
    bool awaiting_input = false;
    int user_response = 0;   // 0=none, 1=wait, 2=disconnect
#if defined(__APPLE__)
    std::atomic<bool> running{false};
    std::atomic<bool> error{false};
#else
    bool running = false;
    bool error = false;
#endif

    // GUI 只写这个"请求"，由 main 里的监视线程去执行——
    // 不能在界面线程里做 join()，否则机器人线程卡在 adb 调用时界面就假死了。
    int request = 0;         // 0=none, 1=restart, 2=abort

    std::mutex mtx;
    std::condition_variable cv;

    void set_status(std::string s) {
        std::lock_guard<std::mutex> lock(mtx);
        status = std::move(s);
    }

    // 阻塞等待用户选择，返回 true=继续等待, false=断开
    bool wait_for_user() {
        std::unique_lock<std::mutex> lock(mtx);
        awaiting_input = true;
        cv.wait(lock, [this] { return user_response != 0; });
        awaiting_input = false;
        bool ret = (user_response == 1);
        user_response = 0;
        return ret;
    }
};
