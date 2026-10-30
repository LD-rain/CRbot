#include "proc.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <thread>

extern char** environ;

namespace {

// 子进程死掉以后再写管道会得到 SIGPIPE，默认动作是杀进程。
// adb 会话被 cancel 时正好会遇到，所以进程内一律忽略（write 返回 EPIPE）。
void ignore_sigpipe() {
    static std::once_flag once;
    std::call_once(once, [] { ::signal(SIGPIPE, SIG_IGN); });
}

bool make_pipe(int fds[2]) {
    if (::pipe(fds) != 0) return false;
    for (int i = 0; i < 2; ++i) ::fcntl(fds[i], F_SETFD, FD_CLOEXEC);
    return true;
}

void close_fd(int& fd) {
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

long long elapsed_ms(std::chrono::steady_clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - since).count();
}

// in_fd 为 -1 时用 /dev/null 顶替 stdin：adb 一般不看 stdin，
// 但把终端留在子进程里容易被它读走按键。
bool spawn_child(const std::vector<std::string>& argv, int in_fd, int out_fd, pid_t* pid) {
    std::vector<char*> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    if (posix_spawn_file_actions_init(&fa) != 0) return false;
    posix_spawn_file_actions_adddup2(&fa, in_fd, STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out_fd, STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, out_fd, STDERR_FILENO);

    const int rc = posix_spawnp(pid, argv[0].c_str(), &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    return rc == 0;
}

}  // namespace

struct ChildSession::Impl {
    pid_t pid = -1;
    int in = -1;   // 写端 -> 子进程 stdin
    int out = -1;  // 读端 <- 子进程 stdout/stderr
};

void cancel_process(CancelState& state) {
    std::lock_guard<std::mutex> lk(state.mtx);
    state.cancelled = true;
    if (state.proc) {
        const auto pid = static_cast<pid_t>(reinterpret_cast<intptr_t>(state.proc));
        ::kill(pid, SIGKILL);
    }
}

ProcResult run_process(const std::vector<std::string>& argv, int timeout_ms,
                       CancelState* cancel) {
    ignore_sigpipe();
    if (argv.empty()) throw std::runtime_error("run_process: empty argv");

    ProcResult res;
    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        if (cancel->cancelled) throw std::runtime_error("process cancelled");
    }

    int out_pipe[2] = {-1, -1};
    if (!make_pipe(out_pipe)) throw std::runtime_error("pipe() failed");

    int devnull = ::open("/dev/null", O_RDONLY);
    if (devnull >= 0) ::fcntl(devnull, F_SETFD, FD_CLOEXEC);

    pid_t pid = -1;
    if (!spawn_child(argv, devnull >= 0 ? devnull : STDIN_FILENO, out_pipe[1], &pid)) {
        close_fd(devnull);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        throw std::runtime_error("cannot spawn: " + argv[0]);
    }
    close_fd(devnull);
    close_fd(out_pipe[1]);
    // drain() must return to the timeout/cancellation loop even when a daemon
    // inherited the pipe, or a child wrote only a partial response.
    ::fcntl(out_pipe[0], F_SETFL, ::fcntl(out_pipe[0], F_GETFL) | O_NONBLOCK);

    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        cancel->proc = reinterpret_cast<void*>(static_cast<intptr_t>(pid));
        if (cancel->cancelled) ::kill(pid, SIGKILL);
    }

    const auto start = std::chrono::steady_clock::now();
    int status = 0;
    bool child_done = false;
    bool timed_out = false;
    auto drain_deadline = std::chrono::steady_clock::time_point::max();

    bool eof = false;  // 读端真到 EOF（没有守护进程占着写端）

    auto drain = [&] {
        for (;;) {
            uint8_t buf[8192];
            const ssize_t n = ::read(out_pipe[0], buf, sizeof buf);
            if (n > 0) {
                res.out.insert(res.out.end(), buf, buf + n);
                continue;
            }
            if (n == 0) eof = true;
            if (n < 0 && errno == EINTR) continue;
            break;
        }
    };

    for (;;) {
        // EOF 之后 fd 会一直是 POLLHUP，继续 poll 会空转烧 CPU（曾经就是这么烧掉 7 秒）
        int r = 0;
        if (!eof) {
            pollfd p{out_pipe[0], POLLIN, 0};
            r = ::poll(&p, 1, child_done ? 50 : 20);
            if (r > 0) drain();
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        if (!child_done) {
            const pid_t w = ::waitpid(pid, &status, WNOHANG);
            if (w == pid || (w < 0 && errno == ECHILD)) {
                child_done = true;
            } else if (elapsed_ms(start) > timeout_ms) {
                ::kill(pid, SIGKILL);
                while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
                }
                timed_out = true;
                child_done = true;
            }
            if (child_done) {
                drain();  // 收掉退出前最后一段输出
                drain_deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
            }
            if (eof && child_done) break;
            continue;
        }

        // 子进程已经退出：EOF 或者管道静默 50ms 就可以收工。
        // 不能只等 EOF —— adb 守护进程也持有这个管道的写端。
        if (eof || r == 0 || std::chrono::steady_clock::now() > drain_deadline) break;
    }

    res.timed_out = timed_out;
    if (WIFEXITED(status)) {
        res.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        res.exit_code = 128 + WTERMSIG(status);
    } else {
        res.exit_code = -1;
    }

    if (cancel) {
        std::lock_guard<std::mutex> lk(cancel->mtx);
        cancel->proc = nullptr;
    }
    close_fd(out_pipe[0]);
    return res;
}

ChildSession::ChildSession() : impl_(std::make_unique<Impl>()) {}
ChildSession::~ChildSession() { stop(); }

bool ChildSession::start(const std::vector<std::string>& argv) {
    ignore_sigpipe();
    stop();

    int in_pipe[2] = {-1, -1};
    int out_pipe[2] = {-1, -1};
    if (!make_pipe(in_pipe) || !make_pipe(out_pipe)) {
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        return false;
    }

    pid_t pid = -1;
    if (!spawn_child(argv, in_pipe[0], out_pipe[1], &pid)) {
        close_fd(in_pipe[0]);
        close_fd(in_pipe[1]);
        close_fd(out_pipe[0]);
        close_fd(out_pipe[1]);
        return false;
    }

    close_fd(in_pipe[0]);
    close_fd(out_pipe[1]);
    impl_->pid = pid;
    impl_->in = in_pipe[1];
    impl_->out = out_pipe[0];
    alive_ = true;
    return true;
}

void ChildSession::stop() {
    alive_ = false;
    if (!impl_) return;
    close_fd(impl_->in);
    close_fd(impl_->out);
    if (impl_->pid > 0) {
        ::kill(impl_->pid, SIGKILL);
        int status = 0;
        while (::waitpid(impl_->pid, &status, 0) < 0 && errno == EINTR) {
        }
        impl_->pid = -1;
    }
}

bool ChildSession::run(const std::string& cmd, const std::string& marker, std::string& out,
                       int timeout_ms) {
    if (!alive_ || impl_->in < 0 || impl_->out < 0) return false;

    const std::string line = cmd + "; echo " + marker + "\n";
    size_t off = 0;
    while (off < line.size()) {
        const ssize_t n = ::write(impl_->in, line.data() + off, line.size() - off);
        if (n > 0) {
            off += static_cast<size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        alive_ = false;
        return false;
    }

    std::string buf;
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        const long long remaining = timeout_ms - elapsed_ms(start);
        if (remaining <= 0) {
            alive_ = false;
            return false;
        }
        pollfd p{impl_->out, POLLIN, 0};
        const int r = ::poll(&p, 1, static_cast<int>(std::min<long long>(remaining, 20)));
        if (r < 0) {
            if (errno == EINTR) continue;
            alive_ = false;
            return false;
        }
        if (r == 0) continue;

        char tmp[4096];
        const ssize_t n = ::read(impl_->out, tmp, sizeof tmp);
        if (n == 0) {
            alive_ = false;
            return false;
        }
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            alive_ = false;
            return false;
        }
        buf.append(tmp, static_cast<size_t>(n));

        const size_t pos = buf.find(marker);
        if (pos != std::string::npos) {
            buf.resize(pos);
            while (!buf.empty() && (buf.back() == '\n' || buf.back() == '\r')) buf.pop_back();
            out = std::move(buf);
            return true;
        }
    }
}
