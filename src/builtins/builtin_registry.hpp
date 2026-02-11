#pragma once

#include <functional>
#include <iosfwd>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace shell {

class HistoryManager;
class PathResolver;

class BuiltinRegistry {
  public:
    using BuiltinFunc = std::function<int(const std::vector<std::string> &, std::ostream &, std::ostream &)>;

    BuiltinRegistry(PathResolver &path_resolver, HistoryManager &history_manager);

    [[nodiscard]] bool is_builtin(std::string_view command) const;
    int execute(std::string_view command, const std::vector<std::string> &args, std::ostream &out, std::ostream &err);

    [[nodiscard]] std::unordered_set<std::string> names() const;
    [[nodiscard]] bool exit_requested() const noexcept;

  private:
    PathResolver &path_resolver_;
    HistoryManager &history_manager_;
    bool exit_requested_{false};
    std::unordered_map<std::string, BuiltinFunc> registry_;

    void register_builtins();

    int builtin_cd(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
    int builtin_echo(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
    int builtin_pwd(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
    int builtin_type(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
    int builtin_history(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
    int builtin_exit(const std::vector<std::string> &args, std::ostream &out, std::ostream &err);
};

} // namespace shell
