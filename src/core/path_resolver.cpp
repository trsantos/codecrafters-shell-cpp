#include "core/path_resolver.hpp"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

namespace shell {

namespace fs = std::filesystem;

void PathResolver::scan_path_executables(
    std::string_view prefix,
    const std::function<bool(std::string_view filename, std::string_view full_path)> &callback) const {
    const char *path_env = std::getenv("PATH");
    if (path_env == nullptr) {
        return;
    }

    std::stringstream path_stream(path_env);
    std::string dir;

    while (std::getline(path_stream, dir, ':')) {
        std::error_code ec;
        for (const auto &entry : fs::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }

            if (!entry.is_regular_file(ec) || ec) {
                continue;
            }

            const std::string filename = entry.path().filename().string();
            if (!filename.starts_with(prefix)) {
                continue;
            }

            const auto perms = fs::status(entry.path(), ec).permissions();
            if (ec) {
                continue;
            }

            constexpr auto executable_bits =
                fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec;

            if ((perms & executable_bits) == fs::perms::none) {
                continue;
            }

            if (callback(filename, entry.path().string())) {
                return;
            }
        }
    }
}

std::string PathResolver::find_command_path(std::string_view command) const {
    std::string resolved_path;

    scan_path_executables(command, [&](std::string_view filename, std::string_view full_path) {
        if (filename == command) {
            resolved_path = full_path;
            return true;
        }

        return false;
    });

    return resolved_path;
}

std::set<std::string> PathResolver::executable_candidates(std::string_view prefix) const {
    std::set<std::string> candidates;

    scan_path_executables(prefix, [&](std::string_view filename, std::string_view /*full_path*/) {
        candidates.emplace(filename);
        return false;
    });

    return candidates;
}

} // namespace shell
