#pragma once

#include <functional>
#include <set>
#include <string>
#include <string_view>

namespace shell {

class PathResolver {
  public:
    [[nodiscard]] std::string find_command_path(std::string_view command) const;
    [[nodiscard]] std::set<std::string> executable_candidates(std::string_view prefix) const;

  private:
    void scan_path_executables(
        std::string_view prefix,
        const std::function<bool(std::string_view filename, std::string_view full_path)> &callback) const;
};

} // namespace shell
