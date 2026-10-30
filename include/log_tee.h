#pragma once
#include <cstddef>

// 把 stdout / stderr 同时写到控制台和日志文件。
// 做法：把它们重定向到一个匿名管道，由一个后台线程把字节同时写给
// 控制台和文件——这样 printf、std::cout、std::cerr 全都能被捕获，
// 不需要改动任何一处现有日志代码。
// 返回 false 表示没装成功（此时行为与原来完全一致，只是没有日志文件）。
bool start_log_tee(const char* path);

// 日志订阅：macOS 的终端面板把日志画在自己的滚动区里，不能直接往 stdout 写
// （会画花界面）。设了 sink 之后，tee 就不再往控制台写，改成把带时间戳的日志字节交给它。
// 传 nullptr 恢复原样。sink 的生命周期由调用方保证，并且要能接收任意线程的调用。
class LogSink {
public:
    virtual ~LogSink() = default;
    virtual void on_log(const char* data, size_t size) = 0;
};

void set_log_sink(LogSink* sink);
