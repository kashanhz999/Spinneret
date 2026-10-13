#pragma once

#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace spinneret::util {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

std::optional<LogLevel> parse_log_level(std::string_view name);

/// Process-wide log sink. Lines look like:
/// `2026-10-13T10:00:00.123Z INFO  [t3] pipeline - [TraceID:abc] message`
class Log {
public:
    static void set_level(LogLevel level);
    static bool enabled(LogLevel level);
    static void write(LogLevel level, std::string_view logger, std::string_view message);
};

/// Trace ID attached to every log line written by the current thread
/// (the C++ counterpart of SLF4J's MDC).
const std::string& current_trace_id();

/// Sets the current thread's trace ID for its lifetime, restoring the previous one afterwards.
class TraceScope {
public:
    explicit TraceScope(std::string trace_id);
    ~TraceScope();
    TraceScope(const TraceScope&) = delete;
    TraceScope& operator=(const TraceScope&) = delete;

private:
    std::string previous_;
};

/// Named logger; cheap to construct, typically a file-local constant.
class Logger {
public:
    explicit constexpr Logger(std::string_view name) : name_(name) {}

    template <class... Args>
    void debug(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogLevel::Debug, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void info(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogLevel::Info, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void warn(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogLevel::Warn, fmt, std::forward<Args>(args)...);
    }
    template <class... Args>
    void error(std::format_string<Args...> fmt, Args&&... args) const {
        emit(LogLevel::Error, fmt, std::forward<Args>(args)...);
    }

private:
    template <class... Args>
    void emit(LogLevel level, std::format_string<Args...> fmt, Args&&... args) const {
        if (!Log::enabled(level)) return;
        Log::write(level, name_, std::format(fmt, std::forward<Args>(args)...));
    }

    std::string_view name_;
};

}  // namespace spinneret::util
