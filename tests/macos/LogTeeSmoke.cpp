#include "log_tee.h"

#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>

struct Capture final : LogSink {
    std::mutex mutex;
    std::condition_variable changed;
    std::string bytes;

    void on_log(const char* data, size_t size) override {
        std::lock_guard lock(mutex);
        bytes.append(data, size);
        changed.notify_all();
    }

    void wait_for(const std::string& suffix) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return bytes.ends_with(suffix); }))
            throw std::runtime_error("Log fragments were not delivered promptly");
    }
};

void write_all(const std::string& bytes) {
    size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count = ::write(STDOUT_FILENO, bytes.data() + offset, bytes.size() - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) throw std::runtime_error("pipe write failed");
        offset += static_cast<size_t>(count);
    }
}

int main(int argc, char** argv) {
    if (argc != 3) return 2;
    Capture capture;
    set_log_sink(&capture);
    if (!start_log_tee(argv[1])) { set_log_sink(nullptr); return 3; }
    try {
        write_all("first\nfragmented ");
        capture.wait_for("fragmented ");
        const std::string chinese = "中文";
        write_all(chinese.substr(0, 1)); // Split inside a UTF-8 code point.
        capture.wait_for(chinese.substr(0, 1));
        write_all(chinese.substr(1) + "\n\nbatch-one\nbatch-two\r\n");
        capture.wait_for("batch-two\r\n");
        std::cout << "iostream" << std::endl;
        std::cerr << "stderr" << std::endl;
        write_all("long:" + std::string(9000, 'x') + "\ntail");
        capture.wait_for("tail"); // An unterminated final line must already be persisted.
        set_log_sink(nullptr);
        std::ofstream sink(argv[2], std::ios::binary);
        sink << capture.bytes;
        return sink ? 0 : 4;
    } catch (const std::exception& error) {
        set_log_sink(nullptr);
        std::cerr << error.what() << std::endl;
        return 1;
    }
}
