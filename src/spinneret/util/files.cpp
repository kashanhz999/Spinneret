#include "spinneret/util/files.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <system_error>

namespace spinneret::util {

namespace fs = std::filesystem;

std::optional<std::string> read_file(const fs::path& path) {
    std::error_code ec;
    if (!fs::exists(path, ec)) return std::nullopt;

    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("Cannot open " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) throw std::runtime_error("Cannot read " + path.string());
    return buffer.str();
}

void atomic_write(const fs::path& path, std::string_view content, bool owner_only) {
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) throw std::runtime_error("Cannot write " + temp.string());
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) throw std::runtime_error("Cannot write " + temp.string());
    }

    std::error_code ec;
    if (owner_only) {
        fs::permissions(temp, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
    }

    fs::rename(temp, path, ec);
    if (ec) {
        // Some platforms refuse to rename over an existing file.
        fs::remove(path, ec);
        fs::rename(temp, path, ec);
        if (ec) throw std::runtime_error("Cannot replace " + path.string() + ": " + ec.message());
    }
}

}  // namespace spinneret::util
