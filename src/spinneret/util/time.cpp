#include "spinneret/util/time.h"

#include <format>

namespace spinneret::util {

std::string iso8601_utc(Clock::time_point tp) {
    auto ms = std::chrono::floor<std::chrono::milliseconds>(tp);
    return std::format("{:%FT%T}Z", ms);
}

long long epoch_millis(Clock::time_point tp) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(tp.time_since_epoch()).count();
}

namespace {

bool read_digits(std::string_view text, std::size_t pos, std::size_t count, int& out) {
    if (pos + count > text.size()) return false;
    int value = 0;
    for (std::size_t i = pos; i < pos + count; ++i) {
        char c = text[i];
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

}  // namespace

std::optional<Clock::time_point> parse_iso8601(std::string_view text) {
    // yyyy-MM-ddTHH:mm:ss
    int year, month, day, hour, minute, second;
    if (text.size() < 19 || text[4] != '-' || text[7] != '-' || text[10] != 'T' ||
        text[13] != ':' || text[16] != ':') {
        return std::nullopt;
    }
    if (!read_digits(text, 0, 4, year) || !read_digits(text, 5, 2, month) ||
        !read_digits(text, 8, 2, day) || !read_digits(text, 11, 2, hour) ||
        !read_digits(text, 14, 2, minute) || !read_digits(text, 17, 2, second)) {
        return std::nullopt;
    }

    std::size_t pos = 19;
    long long millis = 0;
    if (pos < text.size() && text[pos] == '.') {
        ++pos;
        int digits = 0;
        while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
            if (digits < 3) millis = millis * 10 + (text[pos] - '0');
            ++digits;
            ++pos;
        }
        if (digits == 0) return std::nullopt;
        for (int d = digits; d < 3; ++d) millis *= 10;
    }
    if (pos < text.size() && text[pos] == 'Z') ++pos;
    if (pos != text.size()) return std::nullopt;

    std::chrono::year_month_day ymd{std::chrono::year{year},
                                    std::chrono::month{static_cast<unsigned>(month)},
                                    std::chrono::day{static_cast<unsigned>(day)}};
    if (!ymd.ok() || hour > 23 || minute > 59 || second > 60) return std::nullopt;

    return std::chrono::sys_days{ymd} + std::chrono::hours{hour} + std::chrono::minutes{minute} +
           std::chrono::seconds{second} + std::chrono::milliseconds{millis};
}

}  // namespace spinneret::util
