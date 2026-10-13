#include "spinneret/util/logging.h"

#include <atomic>
#include <cstdio>
#include <mutex>

#include "spinneret/util/strings.h"
#include "spinneret/util/time.h"

namespace spinneret::util {

namespace {

std::atomic<int> g_level{static_cast<int>(LogLevel::Info)};
std::mutex g_write_mutex;
std::atomic<int> g_thread_counter{0};

thread_local std::string t_trace_id;

int thread_number() {
    thread_local int number = ++g_thread_counter;
    return number;
}

std::string_view level_name(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO ";
        case LogLevel::Warn: return "WARN ";
        case LogLevel::Error: return "ERROR";
    }
    return "?????";
}

}  // namespace

std::optional<LogLevel> parse_log_level(std::string_view name) {
    auto lower = to_lower(name);
    if (lower == "debug") return LogLevel::Debug;
    if (lower == "info") return LogLevel::Info;
    if (lower == "warn" || lower == "warning") return LogLevel::Warn;
    if (lower == "error") return LogLevel::Error;
    return std::nullopt;
}

void Log::set_level(LogLevel level) { g_level.store(static_cast<int>(level)); }

bool Log::enabled(LogLevel level) { return static_cast<int>(level) >= g_level.load(); }

void Log::write(LogLevel level, std::string_view logger, std::string_view message) {
    std::string line = std::format("{} {} [t{}] {} - [TraceID:{}] {}\n", iso8601_utc(Clock::now()),
                                   level_name(level), thread_number(), logger,
                                   t_trace_id.empty() ? "-" : t_trace_id, message);
    std::lock_guard lock(g_write_mutex);
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fflush(stdout);
}

const std::string& current_trace_id() { return t_trace_id; }

TraceScope::TraceScope(std::string trace_id) : previous_(std::move(t_trace_id)) {
    t_trace_id = std::move(trace_id);
}

TraceScope::~TraceScope() { t_trace_id = std::move(previous_); }

}  // namespace spinneret::util
