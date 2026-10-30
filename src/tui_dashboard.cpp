#include "tui_dashboard.h"
#include "crbot_version.h"

#include "platform.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <string>
#include <thread>

#ifndef CRBOT_VERSION
#define CRBOT_VERSION "dev"
#endif

namespace {

volatile sig_atomic_t g_quit = 0;
void on_signal(int) { g_quit = 1; }

termios g_orig_termios{};
bool g_termios_saved = false;

constexpr size_t kMaxLogLines = 1000;

int tty_rows(int fd) {
    winsize ws{};
    if (fd >= 0 && ::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return ws.ws_row;
    return 24;
}

int tty_cols(int fd) {
    winsize ws{};
    if (fd >= 0 && ::ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return ws.ws_col;
    return 80;
}

// UTF-8 显示宽度（近似）：CJK / 全角算 2 列，其它算 1 列。
int char_cols(uint32_t cp) {
    if (cp < 32) return 0;
    if (cp >= 0x1100 &&
        (cp <= 0x115F || cp == 0x2329 || cp == 0x232A ||
         (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF))) {
        return 2;
    }
    return 1;
}

size_t utf8_decode(const std::string& s, size_t i, uint32_t* cp) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int extra = 0;
    uint32_t v = 0;
    if ((c & 0xE0) == 0xC0) {
        extra = 1;
        v = c & 0x1F;
    } else if ((c & 0xF0) == 0xE0) {
        extra = 2;
        v = c & 0x0F;
    } else if ((c & 0xF8) == 0xF0) {
        extra = 3;
        v = c & 0x07;
    } else {
        *cp = c;
        return 1;
    }
    if (i + static_cast<size_t>(extra) >= s.size()) {
        *cp = c;
        return 1;
    }
    for (int k = 1; k <= extra; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + static_cast<size_t>(k)]);
        if ((cc & 0xC0) != 0x80) {
            *cp = c;
            return 1;
        }
        v = (v << 6) | (cc & 0x3F);
    }
    *cp = v;
    return static_cast<size_t>(extra + 1);
}

std::string fit_cols(const std::string& s, int max_cols) {
    if (max_cols <= 0) return {};
    int used = 0;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = 0;
        const size_t n = utf8_decode(s, i, &cp);
        const int w = char_cols(cp);
        if (used + w > max_cols) break;
        used += w;
        i += n;
    }
    return s.substr(0, i);
}

std::string now_hms() {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d", tm.tm_hour, tm.tm_min, tm.tm_sec);
    return buf;
}

}  // namespace

ConsoleDashboard::ConsoleDashboard(std::vector<DeviceState*>& states) : states_(states) {
    rows_.resize(states_.size());
    log_lines_.emplace_back("[CRBOT] 日志窗口已就绪");
}

ConsoleDashboard::~ConsoleDashboard() { leave_ui(); }

void ConsoleDashboard::enter_ui() {
    tty_fd_ = ::open("/dev/tty", O_RDWR);
    if (tty_fd_ < 0) return;

    if (::tcgetattr(tty_fd_, &g_orig_termios) == 0) {
        g_termios_saved = true;
        termios raw = g_orig_termios;
        raw.c_lflag &= ~(ECHO | ICANON);
        raw.c_iflag &= ~(IXON | ICRNL);
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        ::tcsetattr(tty_fd_, TCSAFLUSH, &raw);
    }

    const char* enter = "\033[?1049h\033[?25l\033[2J";
    (void)!::write(tty_fd_, enter, std::strlen(enter));
    ui_active_ = true;
}

void ConsoleDashboard::leave_ui() {
    if (!ui_active_) {
        if (tty_fd_ >= 0) {
            ::close(tty_fd_);
            tty_fd_ = -1;
        }
        return;
    }
    const char* leave = "\033[?25h\033[?1049l";
    (void)!::write(tty_fd_, leave, std::strlen(leave));
    if (g_termios_saved) ::tcsetattr(tty_fd_, TCSAFLUSH, &g_orig_termios);
    ::close(tty_fd_);
    tty_fd_ = -1;
    ui_active_ = false;
}

void ConsoleDashboard::run() {
    // 没有 pty 的环境（CI / 沙箱）里，导出当前一帧出来检查布局
    if (const char* dump = std::getenv("CRBOT_UI_DUMP"); dump && *dump) {
        set_log_sink(this);  // 日志进日志区，而不是直接打到 stdout
        std::this_thread::sleep_for(std::chrono::seconds(2));
        const std::string text = frame_text(110, 32);
        set_log_sink(nullptr);
        std::ofstream out(dump, std::ios::binary | std::ios::trunc);
        if (out) out << text;
        return;
    }

    struct sigaction sa {};
    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    struct sigaction old_int {}, old_term {};
    ::sigaction(SIGINT, &sa, &old_int);
    ::sigaction(SIGTERM, &sa, &old_term);

    enter_ui();
    if (ui_active_) {
        set_log_sink(this);
        while (!g_quit) {
            pump_input(100);
            render();
            if (!rows_.empty() && sel_ >= static_cast<int>(rows_.size())) {
                sel_ = static_cast<int>(rows_.size()) - 1;
            }
        }
        set_log_sink(nullptr);
        leave_ui();
    } else {
        run_line_mode();
    }

    ::sigaction(SIGINT, &old_int, nullptr);
    ::sigaction(SIGTERM, &old_term, nullptr);
}

void ConsoleDashboard::on_log(const char* data, size_t size) {
    std::lock_guard<std::mutex> lk(log_mtx_);
    log_partial_.append(data, size);

    size_t start = 0;
    for (;;) {
        const size_t nl = log_partial_.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = log_partial_.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        log_lines_.push_back(std::move(line));
        start = nl + 1;
    }
    if (start > 0) log_partial_.erase(0, start);
    while (log_lines_.size() > kMaxLogLines) log_lines_.pop_front();
}

void ConsoleDashboard::refresh_rows() {
    rows_.resize(states_.size());
    for (size_t i = 0; i < states_.size(); ++i) {
        Row& r = rows_[i];
        std::lock_guard<std::mutex> lock(states_[i]->mtx);
        r.serial = states_[i]->serial;
        r.status = states_[i]->status;
        r.awaiting = states_[i]->awaiting_input;
        r.error = states_[i]->error;
        r.running = states_[i]->running;
    }
}

void ConsoleDashboard::render() {
    refresh_rows();

    const std::string frame = build_frame(tty_cols(tty_fd_), tty_rows(tty_fd_));
    if (frame == last_frame_) return;
    last_frame_ = frame;
    (void)!::write(tty_fd_, frame.data(), frame.size());
}

std::string ConsoleDashboard::frame_text(int cols, int rows) {
    refresh_rows();
    std::string frame = build_frame(cols, rows);

    // 去掉 ANSI 转义，方便在文件里看/断言
    std::string out;
    out.reserve(frame.size());
    for (size_t i = 0; i < frame.size();) {
        if (frame[i] != '\033') {
            out.push_back(frame[i]);
            ++i;
            continue;
        }
        size_t j = i + 1;
        if (j < frame.size() && frame[j] == '[') {
            ++j;  // 跳过 '['
            // ANSI 序列的终结字节在 0x40..0x7E，不是只看 A-Z（m / K 都是）
            while (j < frame.size() && !(frame[j] >= '@' && frame[j] <= '~')) ++j;
            if (j < frame.size()) ++j;  // 吃掉终结字节
        }
        i = j;
    }
    return out;
}

std::string ConsoleDashboard::build_frame(int cols, int rows) {
    const int n = static_cast<int>(rows_.size());

    // 固定占 4 行：标题、两条分隔线、底部按键提示；剩下的给设备和日志。
    int dev_rows = n;
    int overflow_row = 0;
    if (n > rows - 5) {
        dev_rows = std::max(0, rows - 6);
        overflow_row = 1;
    }
    const int log_rows = std::max(0, rows - 4 - dev_rows - overflow_row);

    std::string frame;
    frame.reserve(static_cast<size_t>(cols) * static_cast<size_t>(rows) + 512);
    frame += "\033[H";

    // 标题
    frame += fit_cols(std::string(" CRBot ") + CRBOT_VERSION + " · macOS 设备管理器", cols - 10);
    frame += "  " + now_hms();
    frame += "\033[K\n";

    frame += "\033[2m";
    frame += std::string(static_cast<size_t>(std::max(0, cols - 1)), '-');
    frame += "\033[0m\033[K\n";

    if (n == 0) {
        frame += "  没有设备";
        frame += "\033[K\n";
    }
    for (int i = 0; i < dev_rows; ++i) {
        const Row& r = rows_[i];
        char head[64];
        std::snprintf(head, sizeof head, "%s %d  ", i == sel_ ? ">" : " ", i + 1);

        std::string line = head;
        line += fit_cols(r.serial, 26);
        line += "  ";
        if (r.error) line += "!! ";
        if (r.awaiting) line += "[等待确认] ";
        if (!r.running) line += "(stopped) ";
        line += r.status;
        frame += fit_cols(line, cols - 1);
        frame += "\033[K\n";
    }
    if (overflow_row) {
        frame += "  ... 另有 " + std::to_string(n - dev_rows) + " 台设备";
        frame += "\033[K\n";
    }

    frame += "\033[2m";
    frame += std::string(static_cast<size_t>(std::max(0, cols - 1)), '-');
    frame += "\033[0m\033[K\n";

    // 日志区
    std::vector<std::string> tail;
    {
        std::lock_guard<std::mutex> lk(log_mtx_);
        const size_t want = static_cast<size_t>(log_rows);
        const size_t skip = log_lines_.size() > want ? log_lines_.size() - want : 0;
        for (size_t i = skip; i < log_lines_.size(); ++i) tail.push_back(log_lines_[i]);
        if (!log_partial_.empty() && tail.size() < want) tail.push_back(log_partial_);
    }
    for (const std::string& line : tail) {
        frame += " ";
        frame += fit_cols(line, cols - 2);
        frame += "\033[K\n";
    }
    for (int i = static_cast<int>(tail.size()); i < log_rows; ++i) frame += "\033[K\n";

    // 底部按键提示固定在最后一行
    frame += "\033[7m";
    frame += fit_cols(" s=Stop  r=Retry  w=Wait  d=Disconnect  ↑/↓ 或 j/k=选择  q=退出 ", cols);
    frame += "\033[0m\033[K\033[J";
    return frame;
}

void ConsoleDashboard::pump_input(int timeout_ms) {
    pollfd p{tty_fd_, POLLIN, 0};
    const int r = ::poll(&p, 1, timeout_ms);
    if (r <= 0) return;

    char buf[128];
    const ssize_t n = ::read(tty_fd_, buf, sizeof buf);
    if (n <= 0) return;

    for (ssize_t i = 0; i < n; ++i) {
        const char c = buf[i];
        if (c == '\033' && i + 2 < n && buf[i + 1] == '[') {
            if (buf[i + 2] == 'A' && sel_ > 0) --sel_;
            if (buf[i + 2] == 'B' && sel_ + 1 < static_cast<int>(states_.size())) ++sel_;
            i += 2;
            continue;
        }
        handle_key(c);
    }
}

void ConsoleDashboard::handle_key(char key) {
    const int n = static_cast<int>(states_.size());
    switch (key) {
        case 'q':
        case 'Q':
            g_quit = 1;
            return;
        case 'j':
        case 'J':
            if (sel_ + 1 < n) ++sel_;
            return;
        case 'k':
        case 'K':
            if (sel_ > 0) --sel_;
            return;
        case 's':
        case 'S':
            post_request(sel_, 2);  // abort
            return;
        case 'r':
        case 'R':
            post_request(sel_, 1);  // restart
            return;
        case 'w':
        case 'W':
            respond(sel_, 1);  // wait
            return;
        case 'd':
        case 'D':
            respond(sel_, 2);  // disconnect
            return;
        default:
            break;
    }
    if (key >= '1' && key <= '9') {
        const int idx = key - '1';
        if (idx < n) sel_ = idx;
    }
}

void ConsoleDashboard::post_request(int idx, int request) {
    if (idx < 0 || idx >= static_cast<int>(states_.size())) return;
    std::lock_guard<std::mutex> lock(states_[idx]->mtx);
    states_[idx]->request = request;
}

void ConsoleDashboard::respond(int idx, int response) {
    if (idx < 0 || idx >= static_cast<int>(states_.size())) return;
    std::lock_guard<std::mutex> lock(states_[idx]->mtx);
    if (!states_[idx]->awaiting_input) return;
    states_[idx]->user_response = response;
    states_[idx]->cv.notify_all();
}

void ConsoleDashboard::run_line_mode() {
    std::printf("[CRBOT] 没有可用的终端，进入行模式\n");
    std::printf("[CRBOT] 命令：quit | stop N | retry N | wait N | disconnect N\n");
    std::fflush(stdout);

    std::vector<std::string> last(states_.size());
    std::string inbuf;
    bool stdin_open = true;
    auto last_tick = std::chrono::steady_clock::now();

    while (!g_quit) {
        if (stdin_open) {
            pollfd p{STDIN_FILENO, POLLIN, 0};
            if (::poll(&p, 1, 200) > 0) {
                char buf[256];
                const ssize_t n = ::read(STDIN_FILENO, buf, sizeof buf);
                if (n <= 0) {
                    stdin_open = false;
                } else {
                    inbuf.append(buf, static_cast<size_t>(n));
                    size_t pos = 0;
                    while ((pos = inbuf.find('\n')) != std::string::npos) {
                        std::string line = inbuf.substr(0, pos);
                        inbuf.erase(0, pos + 1);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty()) continue;
                        const auto sp = line.find(' ');
                        const std::string cmd = line.substr(0, sp);
                        int idx = sel_;
                        if (sp != std::string::npos) {
                            try {
                                idx = std::stoi(line.substr(sp + 1)) - 1;
                            } catch (...) {
                                idx = -1;
                            }
                        }
                        if (cmd == "quit" || cmd == "q") {
                            g_quit = 1;
                        } else if (cmd == "stop") {
                            post_request(idx, 2);
                        } else if (cmd == "retry") {
                            post_request(idx, 1);
                        } else if (cmd == "wait") {
                            respond(idx, 1);
                        } else if (cmd == "disconnect") {
                            respond(idx, 2);
                        } else {
                            std::printf("[CRBOT] 不认识的命令：%s\n", cmd.c_str());
                        }
                    }
                }
            }
        } else {
            platform::sleep_ms(200);
        }

        const auto now = std::chrono::steady_clock::now();
        if (now - last_tick >= std::chrono::seconds(1)) {
            last_tick = now;
            for (size_t i = 0; i < states_.size(); ++i) {
                std::string status;
                {
                    std::lock_guard<std::mutex> lock(states_[i]->mtx);
                    status = states_[i]->status;
                }
                if (status != last[i]) {
                    last[i] = status;
                    std::printf("[STATUS] %s: %s\n", states_[i]->serial.c_str(), status.c_str());
                }
            }
            std::fflush(stdout);
        }
    }
}
